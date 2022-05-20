[ComponentEditorProps(category: "GameScripted/GameMode/Components", description: "Cluck cluck!", color: "0 0 255 255")]
class SCR_ChickenGameModeComponentClass : SCR_BaseGameModeComponentClass
{
};

//------------------------------------------------------------------------------------------------
/*!
	Server "cluck" data.
*/
class SCR_ChickenInfo
{
	protected bool m_bIsClucking;
	protected float m_fCluckTimer = 0.0;
	protected vector m_vLastPosition;

	protected IEntity m_pChicken;

	//------------------------------------------------------------------------------------------------
	void Update(IEntity chickenEntity, float timeSlice, float thresholdSq, float maxTime)
	{
		IEntity lastEntity = GetChickenEntity();
		SetChickenEntity(chickenEntity);

		if (lastEntity != chickenEntity)
			OnEntityChanged(lastEntity, chickenEntity);

		if (!m_pChicken)
		{
			m_fCluckTimer = 0.0;
			return;
		}

		vector newPosition = m_pChicken.GetOrigin();
		float currentDistance = vector.DistanceSq(newPosition, m_vLastPosition);
		if (currentDistance < thresholdSq)
		{
			m_fCluckTimer += timeSlice;
			if (m_fCluckTimer > maxTime)
				m_fCluckTimer = maxTime;
		}
		else
		{
			if (m_fCluckTimer > 0.0)
				m_fCluckTimer -= timeSlice;
			else if (m_fCluckTimer < 0.0)
				m_fCluckTimer = 0.0;
		}

		m_vLastPosition = newPosition;
	}

	//------------------------------------------------------------------------------------------------
	bool ShouldCluck(float timer)
	{
		return m_fCluckTimer >= timer;
	}

	//------------------------------------------------------------------------------------------------
	void SetCluck(bool isClucking)
	{
		m_bIsClucking = isClucking;
	}

	//------------------------------------------------------------------------------------------------
	bool GetCluck()
	{
		return m_bIsClucking;
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity GetChickenEntity()
	{
		return m_pChicken;
	}

	//------------------------------------------------------------------------------------------------
	protected void SetChickenEntity(IEntity entity)
	{
		m_pChicken = entity;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnEntityChanged(IEntity previous, IEntity next)
	{
		m_fCluckTimer = 0.0; // Reset timer on entity changed
	}

}

//------------------------------------------------------------------------------------------------
//! Cluck cluck if you camp for too long!
class SCR_ChickenGameModeComponent : SCR_BaseGameModeComponent
{
	//! Array of all connected players by their playerId
	protected ref map<int, ref SCR_ChickenInfo> m_aChickenInfo;

	[Attribute("3.5", UIWidgets.Slider, "Cluck timer in seconds. Starts clucking after this period.", "0 300 0.001", precision: 4)]
	protected float m_fCluckTimer;

	[Attribute("0.05", UIWidgets.Slider, "Distance threshold below which movement is deemed static.", "0 1 0.001", precision: 4)]
	protected float m_fCluckDistance;

	[Attribute("{C119F1A1675A3E73}Sounds/Chicken.acp", UIWidgets.Auto, "Clucking definition.", params: "acp")]
	protected ResourceName m_rAudioProject;

	[Attribute("CLUCK_1_SOUND", UIWidgets.EditBox, "Clucking sound event.", params: "")]
	protected string m_sAudioEvent;

	//! Map of runtime audio handles
	protected ref map<int, AudioHandle> m_mHandles = new map<int, AudioHandle>();

	//! Local session data - show hint if clucking for the first time
	protected bool m_bHasClucked;

	//------------------------------------------------------------------------------------------------
	protected float GetCluckTimer()
	{
		return m_fCluckTimer;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetCluckDistance()
	{
		return m_fCluckDistance;
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	protected override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		SetEventMask(owner, EntityEvent.FRAME);

		// Authority only will tick
		if (!m_pGameMode.IsMaster())
			return;

		m_aChickenInfo = new map<int, ref SCR_ChickenInfo>();
		SetEventMask(owner, EntityEvent.POSTFIXEDFRAME);
	}

	//------------------------------------------------------------------------------------------------
	protected override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		// Continue playing audio until handle is done - when RPC with cluck stop request comes, the handle is removed
		foreach (int playerId, AudioHandle handle : m_mHandles)
		{
			IEntity ent = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
			if (!ent)
				continue;

			// I believe this could be done better,
			// but I would have to ask the audio guys, I never worked with audio before
			// also this could preferably be cached
			BaseSoundComponent soundComponent = BaseSoundComponent.Cast(ent.FindComponent(BaseSoundComponent));
			if (!soundComponent)
				continue;

			vector transformMatrix[4];
			ent.GetWorldTransform(transformMatrix);



			if (!handle || soundComponent.IsFinishedPlaying(handle))
			{
				soundComponent.Terminate(handle);
				soundComponent.SetTransformation(transformMatrix);
				m_mHandles[playerId] = soundComponent.PlayStr(m_sAudioEvent);

			}
		}
	}


	//------------------------------------------------------------------------------------------------
	//! Transformation of certain items occurs during fixed frame, so iterate over them during post fixed frame
	protected override void EOnPostFixedFrame(IEntity owner, float timeSlice)
	{
		super.EOnPostFixedFrame(owner, timeSlice);

		// Update all chicken data
		float cluckDistanceSq = Math.Pow(GetCluckDistance(), 2.0);
		float cluckTimer = GetCluckTimer();
		PlayerManager playerManager = GetGame().GetPlayerManager();

		// Update all server chicken data
		foreach (int playerId, SCR_ChickenInfo chickenInfo : m_aChickenInfo)
		{
			IEntity controlledEntity = playerManager.GetPlayerControlledEntity(playerId);
			chickenInfo.Update(controlledEntity, timeSlice, cluckDistanceSq, cluckTimer);

			// Handle clucking state
			if (chickenInfo.ShouldCluck(cluckTimer))
			{
				if (!chickenInfo.GetCluck())
				{
					chickenInfo.SetCluck(true);
					OnPlayerCluckBegin(playerId);
				}
			}
			else if (chickenInfo.GetCluck())
			{
				chickenInfo.SetCluck(false);
				OnPlayerCluckEnd(playerId);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPlayerCluckBegin(int playerId)
	{

		if (!m_pGameMode.IsMaster())
			return;

		// Authority clucks locally
		Rpc_StartCluck(playerId);
		// Authority sends cluck to proxies
		Rpc(Rpc_StartCluck, playerId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void Rpc_StartCluck(int playerId)
	{
		// Print("Player " + playerId + "started clucking!");

		// Show hint for clucking,
		if (!m_bHasClucked)
		{
			PlayerController playerController = GetGame().GetPlayerController();
			if (playerController && playerId == playerController.GetPlayerId())
			{
				SCR_HintManagerComponent hintsManager = SCR_HintManagerComponent.GetInstance();
				if (hintsManager)
				{
					string description = string.Format("Be on the move or you'll start clucking! Standing still for %1 seconds gets you clucking!", GetCluckTimer());
					hintsManager.ShowCustom(description, "Cluck Cluck!");
				}
			}

			m_bHasClucked = true;
		}


		IEntity ent = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!ent)
			return;

		BaseSoundComponent soundComponent = BaseSoundComponent.Cast(ent.FindComponent(BaseSoundComponent));
		if (!soundComponent)
			return;

		AudioHandle hnd;
		if (m_mHandles.Contains(playerId))
		{
			hnd = m_mHandles[playerId];
			soundComponent.Terminate(hnd);
		}

		m_mHandles.Insert(playerId, soundComponent.PlayStr(m_sAudioEvent));
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPlayerCluckEnd(int playerId)
	{
		if (!m_pGameMode.IsMaster())
			return;

		// Authority clucks locally
		Rpc_EndCluck(playerId);
		// Authority sends cluck to proxies
		Rpc(Rpc_EndCluck, playerId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void Rpc_EndCluck(int playerId)
	{
		if (m_mHandles.Contains(playerId))
		{
			AudioHandle hnd = m_mHandles[playerId];
			if (hnd)
				AudioSystem.TerminateSound(hnd);

			m_mHandles.Remove(playerId);
		}

		//Print("Player " + playerId + "stopped clucking!");
	}

	//------------------------------------------------------------------------------------------------
	protected override bool RplSave(ScriptBitWriter writer)
	{
		super.RplSave(writer);

		// Read number of audio handles; ie. playing sounds
		int count = m_mHandles.Count();
		writer.WriteInt(count);

		// Write id of each player playing sound
		foreach (int playerId, AudioHandle hnd : m_mHandles)
			writer.WriteInt(playerId);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected override bool RplLoad(ScriptBitReader reader)
	{
		super.RplLoad(reader);

		// Read number of audio handles; ie. playing sounds
		int count;
		reader.ReadInt(count);
		array<int> playerIds = {};

		for (int i = 0; i < count; i++)
		{
			int playerId;
			reader.ReadInt(playerId);
			playerIds.Insert(playerId);
		}

		// Start clucking locally
		foreach (int playerId : playerIds)
			Rpc_StartCluck(playerId);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnPlayerConnected(int playerId)
	{
		// Server only calls

		m_aChickenInfo.Insert(playerId, new SCR_ChickenInfo());
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnPlayerDisconnected(int playerId)
	{
		// Server only calls
		SCR_ChickenInfo chicken = m_aChickenInfo[playerId];

		// Make sure to always stop clucking!
		if (chicken && chicken.GetCluck())
			OnPlayerCluckEnd(playerId);

		m_aChickenInfo.Remove(playerId);
	}
};
