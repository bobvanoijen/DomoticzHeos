#include "stdafx.h"
#include "HEOS.h"
#include <boost/lexical_cast.hpp>
#include "../hardware/hardwaretypes.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../main/SQLHelper.h"
#include "../notifications/NotificationHelper.h"
#include "../main/WebServer.h"
#include "../main/mainworker.h"
#include "../main/localtime_r.h"
#include "../main/EventSystem.h"
#include "../webserver/cWebem.h"
#include <boost/algorithm/string.hpp>

#include <iostream>

CHEOS::CHEOS(const int ID, const std::string &IPAddress, const int Port, const std::string &User, const std::string &Pwd, const int PollIntervalsec, const int PingTimeoutms) : 
m_IP(IPAddress),
m_User(User),
m_Pwd(Pwd),
m_stoprequested(false),
m_iThreadsRunning(0)
{
	m_HwdID = ID;
	m_Port = Port;
	m_bShowedStartupMessage = false;
	m_iMissedQueries = 0;
	SetSettings(PollIntervalsec, PingTimeoutms);
}

CHEOS::CHEOS(const int ID) : m_stoprequested(false), m_iThreadsRunning(0)
{
	m_HwdID = ID;
	m_IP = "";
	m_Port = 0;
	m_User = "";
	m_Pwd = "";
	m_bShowedStartupMessage = false;
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT Address, Port, Username, Password FROM Hardware WHERE ID==%d", m_HwdID);

	if (result.size() > 0)
	{
		m_IP = result[0][0];
		m_Port = atoi(result[0][1].c_str());
		m_User = result[0][2];
		m_Pwd = result[0][3];
	}

	SetSettings(10, 3000);
}

CHEOS::~CHEOS(void)
{
	m_bIsStarted = false;
}

void CHEOS::handleMessage(std::string& pMessage)
{
	try
	{
		Json::Reader jReader;
		Json::Value root;
		std::string	sMessage;
		std::stringstream ssMessage;

		if (DEBUG_LOGGING) _log.Log(LOG_NORM, "DENON by HEOS: Handling message: '%s'.", pMessage.c_str());
		bool	bRetVal = jReader.parse(pMessage, root);
		if (!bRetVal)
		{
			_log.Log(LOG_ERROR, "DENON by HEOS: PARSE ERROR: '%s'", pMessage.c_str());
		}
		else
		{
			// HEOS generated messages
			if (root.isMember("heos"))
			{
				if (root["heos"].isMember("result"))
				{
					if (root["heos"]["result"] == "success")
					{
						if (root["heos"].isMember("command"))
						{
							if (root["heos"]["command"] == "system/heart_beat")
							{
								
							}
							else if (root["heos"]["command"] == "player/get_players")
							{
								if (root.isMember("payload"))
								{
									for( Json::ValueIterator itr = root["payload"].begin() ; itr != root["payload"].end() ; itr++ ) {
										if (root["payload"][itr].isMember("name") && root["payload"][itr].isMember("pid"))
										{
											AddNode(root["payload"][itr]["name"], root["payload"][itr]["pid"]);
										}
										else
										{
											if (DEBUG_LOGGING) _log.Log(LOG_NORM, "DENON by HEOS: No players found.");
										}
									}
								}
								else
								{
									if (DEBUG_LOGGING) _log.Log(LOG_NORM, "DENON by HEOS: No players found (No Payload).");
								}
							}
							else if (root["heos"]["command"] == "player/get_play_state")
							{
								if (root["heos"].isMember("message"))
								{
									std::vector<std::string> SplitMessage;
									StringSplit(root["heos"]["message"].asString(), "&", SplitMessage);
									if (SplitMessage.size() > 0)
									{
										std::vector<std::string> SplitMessagePlayer;
										StringSplit(SplitMessage[0], "=", SplitMessagePlayer);
										std::vector<std::string> SplitMessageState;
										StringSplit(SplitMessage[1], "=", SplitMessageState);
										std::string pid = SplitMessagePlayer[1];
										std::string state = SplitMessageState[1];										
									}
								}
							}
							else if (root["heos"]["command"] == "player/set_play_state")
							{
								if (root["heos"].isMember("message"))
								{
									std::vector<std::string> SplitMessage;
									StringSplit(root["heos"]["message"].asString(), "&", SplitMessage);
									if (SplitMessage.size() > 0)
									{
										std::vector<std::string> SplitMessagePlayer;
										StringSplit(SplitMessage[0], "=", SplitMessagePlayer);
										std::vector<std::string> SplitMessageState;
										StringSplit(SplitMessage[1], "=", SplitMessageState);
										std::string pid = SplitMessagePlayer[1];
										std::string state = SplitMessageState[1];										
									}
								}
							}
						}
					}
					else
					{
						if (root["heos"].isMember("command"))
						{
							if (DEBUG_LOGGING) _log.Log(LOG_NORM, "DENON by HEOS: Failed: '%s'.", root["heos"]["command"]);	
						}	
					}
				}
			}
			else
			{
				if (DEBUG_LOGGING) _log.Log(LOG_NORM, "DENON by HEOS: Message not generated by HEOS System.");
			}
			
		}	
	}
	catch (std::exception& e)
	{
		_log.Log(LOG_ERROR, "DENON by HEOS: Exception: %s", e.what());
	}
}

void CHEOS::handleConnect()
{
	try
	{
		if (!m_stoprequested && !m_Socket)
		{
			m_iMissedPongs = 0;
			boost::system::error_code ec;
			boost::asio::ip::tcp::resolver resolver(*m_Ios);
			boost::asio::ip::tcp::resolver::query query(m_IP, (m_Port[0] != '-' ? m_Port : m_Port.substr(1)));
			boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve(query);
			boost::asio::ip::tcp::endpoint endpoint = *iter;
			m_Socket = new boost::asio::ip::tcp::socket(*m_Ios);
			m_Socket->connect(endpoint, ec);
			if (!ec)
			{
				_log.Log(LOG_NORM, "HEOS by DENON: Connected to '%s:%s'.", m_IP.c_str(), (m_Port[0] != '-' ? m_Port.c_str() : m_Port.substr(1).c_str()));
				if (m_CurrentStatus.Status() == MSTAT_OFF)
				{
					m_CurrentStatus.Clear();
					m_CurrentStatus.Status(MSTAT_ON);
					UpdateStatus();
				}
				m_Socket->async_read_some(boost::asio::buffer(m_Buffer, sizeof m_Buffer), boost::bind(&CHEOS::handleRead, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
				// Disable registration for change events following HEOS Controller advise
				handleWrite(std::string("heos://system/register_for_change_events?enable=off"));
			}
			else
			{
				if ((DEBUG_LOGGING) ||
					(
						(ec.value() != 113) &&
						(ec.value() != 111) &&
						(ec.value() != 10060) &&
						(ec.value() != 10061) &&
						(ec.value() != 10064)
						)
					) // Connection failed due to no response, no route or active refusal
				{
					_log.Log(LOG_NORM, "HEOS by DENON: Connect to '%s:%s' failed: (%d) %s", m_IP.c_str(), (m_Port[0] != '-' ? m_Port.c_str() : m_Port.substr(1).c_str()), ec.value(), ec.message().c_str());
				}
				delete m_Socket;
				m_Socket = NULL;
				m_CurrentStatus.Clear();
				m_CurrentStatus.Status(MSTAT_OFF);
				UpdateStatus();
			}
		}
	}
	catch (std::exception& e)
	{
		_log.Log(LOG_ERROR, "HEOS by DENON: Exception: '%s' connecting to '%s'", e.what(), m_IP.c_str());
	}
}

void CHEOS::handleRead(const boost::system::error_code& e, std::size_t bytes_transferred)
{
	if (!e)
	{
		//Start reading and processing the data
		std::string sData(m_Buffer.begin(), bytes_transferred);
		sData = m_RetainedData + sData;  // if there was some data left over from last time add it back in
		int iPos = 1;
		while (iPos) {
			iPos = sData.find("}{", 0) + 1;		//  Look for message a separator in case there is more than one
			if (!iPos) // no, just one or part of one
			{
				if ((sData.substr(sData.length()-1, 1) == "}") &&
					(std::count(sData.begin(), sData.end(), '{') == std::count(sData.begin(), sData.end(), '}'))) // whole message so process
				{
					handleMessage(sData);
					sData = "";
				}
			}
			else  // more than one message so process the first one
			{
				std::string sMessage = sData.substr(0, iPos);
				sData = sData.substr(iPos);
				handleMessage(sMessage);
			}
		}
		m_RetainedData = sData;

		//ready for next read
		if (!m_stoprequested && m_Socket)
			m_Socket->async_read_some(	boost::asio::buffer(m_Buffer, sizeof m_Buffer),
										boost::bind(&CHEOS::handleRead, 
										shared_from_this(),
										boost::asio::placeholders::error,
										boost::asio::placeholders::bytes_transferred));
	}
	else
	{
		if (e.value() != 1236)		// Local disconnect caused by hardware reload
		{
			if ((e.value() != 2) && (e.value() != 121))	// Semaphore time-out expiry or end of file aka 'lost contact'
				_log.Log(LOG_ERROR, "HEOS by DENON: Async Read Exception: %d, %s", e.value(), e.message().c_str());
			m_CurrentStatus.Clear();
			m_CurrentStatus.Status(MSTAT_OFF);
			UpdateStatus();
			handleDisconnect();
		}
	}
}

void CHEOS::handleWrite(std::string pMessage)
{
	if (!m_stoprequested) {
		if (m_Socket)
		{
			if (DEBUG_LOGGING) _log.Log(LOG_NORM, "HEOS by DENON: Sending data: '%s'", pMessage.c_str());
			m_Socket->write_some(boost::asio::buffer(pMessage.c_str(), pMessage.length()));
			m_sLastMessage = pMessage;
		}
		else 
		{
			_log.Log(LOG_ERROR, "HEOS by DENON: Data not sent to NULL socket: '%s'", pMessage.c_str());
		}
	}
}

void CHEOS::handleDisconnect()
{
	if (m_Socket)
	{
		_log.Log(LOG_NORM, "HEOS by DENON: Disconnected");
		boost::system::error_code	ec;
		m_Socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		m_Socket->close();
		delete m_Socket;
		m_Socket = NULL;
	}
}

void CHEOS::SendCommand(const std::string &command)
{
	std::stringstream ssMessage;
	std::string	sMessage;
	bool systemCall = false;

	// Register for change events
	if (command == "registerForEvents")
	{
		ssMessage << "heos://system/register_for_change_events?enable=on";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	// Unregister for change events
	if (command == "registerForEvents")
	{
		ssMessage << "heos://system/register_for_change_events?enable=off";
		sMessage = ssMessage.str();
		systemCall = true;
	}

	if (command == "heartbeat")
	{
		ssMessage << "heos://system/heart_beat";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	if (command == "getPlayers")
	{
		ssMessage << "heos://player/get_players";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	/* Group related commands */

	if (command == "getGroups")
	{
		ssMessage << "heos://group/get_groups";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	/* Process */
	
	if (sMessage.length())
	{
		if (m_Socket != NULL)
		{
			handleWrite(sMessage);
			if (systemCall)
			{
				if (DEBUG_LOGGING) _log.Log(LOG_NORM, "HEOS by DENON: Sent command: '%s'.", sMessage.c_str());	
			}
			else
			{
				_log.Log(LOG_NORM, "HEOS by DENON: Sent command: '%s'.", sMessage.c_str());				
			}				
		}
		else
		{
			_log.Log(LOG_NORM, "HEOS by DENON: Command not sent, HEOS is not connected: '%s'.", sMessage.c_str());
		}
	}
	else
	{
		_log.Log(LOG_ERROR, "HEOS by DENON: Command: '%s'. Unknown command.", command.c_str());
	}
}

void CHEOS::SendCommand(const std::string &command, const int iValue)
{
	std::stringstream ssMessage;
	std::string	sMessage;
	bool systemCall = false;

	if (command == "getPlayerInfo")
	{
		ssMessage << "heos://player/get_player_info?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	if (command == "getPlayState")
	{
		ssMessage << "heos://player/get_play_state?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	if (command == "setPlayStatePlay")
	{
		ssMessage << "heos://player/set_play_state?pid=" << iValue << "&state=play";
		sMessage = ssMessage.str();
	}
	
	if (command == "setPlayStatePause")
	{
		ssMessage << "heos://player/set_play_state?pid=" << iValue << "&state=pause";
		sMessage = ssMessage.str();
	}
	
	if (command == "setPlayStateStop")
	{
		ssMessage << "heos://player/set_play_state?pid=" << iValue << "&state=stop";
		sMessage = ssMessage.str();
	}
	
	if (command == "getNowPlaying")
	{
		ssMessage << "heos://player/get_now_playing_media?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	if (command == "getVolume")
	{
		ssMessage << "heos://player/get_volume?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	if (command == "setVolumeUp")
	{
		ssMessage << "heos://player/volume_up?pid=" << iValue << "";
		sMessage = ssMessage.str();
	}
	
	if (command == "setVolumeDown")
	{
		ssMessage << "heos://player/volume_up?pid=" << iValue << "";
		sMessage = ssMessage.str();
	}
	
	if (command == "getMute")
	{
		ssMessage << "heos://player/get_mute?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	if (command == "setMuteOn")
	{
		ssMessage << "heos://player/set_mute?pid=" << iValue << "&state=on";
		sMessage = ssMessage.str();
	}

	if (command == "setMuteOff")
	{
		ssMessage << "heos://player/set_mute?pid=" << iValue << "&state=off";
		sMessage = ssMessage.str();
	}
	
	if (command == "toggleMute")
	{
		ssMessage << "heos://player/toggle_mute?pid=" << iValue << "";
		sMessage = ssMessage.str();
	}

	if (command == "getPlayMode")
	{
		ssMessage << "heos://player/get_play_mode?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	
	/* Set playmode
	if (command == "setPlayMode")
	{
		ssMessage << "heos://player/volume_up?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}
	*/

	/* Queue related commands */
	
	if (command == "getQueue")
	{
		ssMessage << "heos://player/get_queue?pid=" << iValue << "";
		sMessage = ssMessage.str();
		systemCall = true;
	}

	if (command == "playNext")
	{
		ssMessage << "heos://player/play_next?pid=" << iValue << "";
		sMessage = ssMessage.str();
	}
	
	if (command == "playPrev")
	{
		ssMessage << "heos://player/play_previous?pid=" << iValue << "";
		sMessage = ssMessage.str();
	}
	
	/* Process */
	
	if (sMessage.length())
	{
		if (m_Socket != NULL)
		{
			handleWrite(sMessage);
			if (systemCall)
			{
				if (DEBUG_LOGGING) _log.Log(LOG_NORM, "HEOS by DENON: Sent command: '%s'.", sMessage.c_str());	
			}
			else
			{
				_log.Log(LOG_NORM, "HEOS by DENON: Sent command: '%s'.", sMessage.c_str());				
			}				
		}
		else
		{
			_log.Log(LOG_NORM, "HEOS by DENON: Command not sent, HEOS is not connected: '%s'.", sMessage.c_str());
		}
	}
	else
	{
		_log.Log(LOG_ERROR, "HEOS by DENON: Command: '%s'. Unknown command.", command.c_str());
	}
}

void CHEOS::Do_Work()
{
	m_Busy = true;
	int mcounter = 0;
	int scounter = 0;
	bool bFirstTime = true;

	_log.Log(LOG_STATUS, "HEOS by DENON: Worker started...");

	ReloadNodes();

	while (!m_stoprequested)
	{
		sleep_milliseconds(500);		
		mcounter++;

		if (!m_Socket)
		{
			handleConnect();
		}
		
		if (mcounter == 2)
		{
			mcounter = 0;
			scounter++;
			if ((scounter >= m_iPollInterval) || (bFirstTime))
			{

				scounter = 0;
				bFirstTime = false;

				SendCommand("getPlayers");

				std::vector<HEOSNode>::const_iterator itt;
				for (itt = m_nodes.begin(); itt != m_nodes.end(); ++itt)
				{
					if (m_stoprequested)
						return;
					if (m_iThreadsRunning < 1000)
					{
						m_iThreadsRunning++;
						SendCommand("getPlayState", *itt);
					}
				}
			}
			else
			{
				if(m_iMissedQueries > 5)
				{
						_log.Log(LOG_NORM, "HEOS by DENON Missed 5 commands, assumed off.");
						boost::system::error_code	ec;
						m_Socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
						continue;
				};
			}
			
		}
	}

	m_Busy = false;
	delete m_Socket;
	m_Socket = NULL;

	_log.Log(LOG_STATUS, "HEOS by DENON: Worker stopped...");
	
}

_eNotificationTypes	CHEOS::NotificationType(_eMediaStatus nStatus)
{
	switch (nStatus)
	{
	case MSTAT_OFF:		return NTYPE_SWITCH_OFF;
	case MSTAT_ON:		return NTYPE_SWITCH_ON;
	case MSTAT_PAUSED:	return NTYPE_PAUSED;
	case MSTAT_STOPPED:	return NTYPE_STOPPED;
	case MSTAT_PLAYING:	return NTYPE_PLAYING;
	default:			return NTYPE_SWITCH_OFF;
	}
}

bool CHEOS::StartHardware()
{
	StopHardware();
	m_bIsStarted = true;
	sOnConnected(this);
	m_iThreadsRunning = 0;
	m_bShowedStartupMessage = false;

	StartHeartbeatThread();

	//Start worker thread
	m_stoprequested = false;
	m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CHEOS::Do_Work, this)));

	return (m_thread != NULL);
}

bool CHEOS::StopHardware()
{
	StopHeartbeatThread();

	try {
		if (m_thread)
		{
			m_stoprequested = true;
			m_thread->join();
			m_thread.reset();

			//Make sure all our background workers are stopped
			int iRetryCounter = 0;
			while ((m_iThreadsRunning > 0) && (iRetryCounter<15))
			{
				sleep_milliseconds(500);
				iRetryCounter++;
			}
		}
	}
	catch (...)
	{
		//Don't throw from a Stop command
	}
	m_bIsStarted = false;
	return true;
}

void CHEOS::UpdateNodeStatus(const LogitechMediaServerNode &Node, const _eMediaStatus nStatus, const std::string &sStatus, bool bPingOK)
{
	//Find out node, and update it's status
	std::vector<LogitechMediaServerNode>::iterator itt;
	for (itt = m_nodes.begin(); itt != m_nodes.end(); ++itt)
	{
		if (itt->ID == Node.ID)
		{
			//Found it
			//Retrieve devicename instead of playername in case it was renamed...
			std::string sDevName = itt->Name;
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Name FROM DeviceStatus WHERE DeviceID=='%q'", itt->szDevID);
			if (result.size() == 1)	{
				std::vector<std::string> sd = result[0];
				sDevName = sd[0];
			}
			bool	bUseOnOff = false;
			if (((nStatus == MSTAT_OFF) && bPingOK) || ((nStatus != MSTAT_OFF) && !bPingOK)) bUseOnOff = true;
			time_t atime = mytime(NULL);
			itt->LastOK = atime;
			if ((itt->nStatus != nStatus) || (itt->sStatus != sStatus))
			{
				// 1:	Update the DeviceStatus
				if ((nStatus == MSTAT_PLAYING) || (nStatus == MSTAT_PAUSED) || (nStatus == MSTAT_STOPPED))
					_log.Log(LOG_NORM, "Logitech Media Server: (%s) %s - '%s'", Node.Name.c_str(), Media_Player_States(nStatus), sStatus.c_str());
				else
					_log.Log(LOG_NORM, "Logitech Media Server: (%s) %s", Node.Name.c_str(), Media_Player_States(nStatus));
				struct tm ltime;
				localtime_r(&atime, &ltime);
				char szLastUpdate[40];
				sprintf(szLastUpdate, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("UPDATE DeviceStatus SET nValue=%d, sValue='%q', LastUpdate='%q' WHERE (HardwareID == %d) AND (DeviceID == '%q') AND (Unit == 1) AND (SwitchType == %d)",
					int(nStatus), sStatus.c_str(), szLastUpdate, m_HwdID, itt->szDevID, STYPE_Media);

				// 2:	Log the event if the actual status has changed
				std::string sShortStatus = sStatus;
				if ((itt->nStatus != nStatus) || (itt->sShortStatus != sShortStatus))
				{
					std::string sLongStatus = Media_Player_States(nStatus);
					if ((nStatus == MSTAT_PLAYING) || (nStatus == MSTAT_PAUSED) || (nStatus == MSTAT_STOPPED))
						if (sShortStatus.length()) sLongStatus += " - " + sShortStatus;
					result = m_sql.safe_query("INSERT INTO LightingLog (DeviceRowID, nValue, sValue) VALUES (%d, %d, '%q')", itt->ID, int(nStatus), sLongStatus.c_str());
				}

				// 3:	Trigger On/Off actions
				if (bUseOnOff)
				{
					result = m_sql.safe_query("SELECT StrParam1,StrParam2 FROM DeviceStatus WHERE (HardwareID==%d) AND (ID = '%q') AND (Unit == 1)", m_HwdID, itt->szDevID);
					if (result.size() > 0)
					{
						m_sql.HandleOnOffAction(bPingOK, result[0][0], result[0][1]);
					}
				}

				// 4:   Trigger Notifications & events on status change
				if (itt->nStatus != nStatus)
				{
					m_notifications.CheckAndHandleNotification(itt->ID, sDevName, NotificationType(nStatus), sStatus.c_str());
					m_mainworker.m_eventsystem.ProcessDevice(m_HwdID, itt->ID, 1, int(pTypeLighting2), int(sTypeAC), 12, 100, int(nStatus), sStatus.c_str(), sDevName, 0);
				}

				itt->nStatus = nStatus;
				itt->sStatus = sStatus;
				itt->sShortStatus = sShortStatus;
			}
			break;
		}
	}
}

void CHEOS::AddNode(const std::string &Name, const std::string &PlayerID)
{
	//Check if exists
	result = m_sql.safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q')", m_HwdID, PlayerID.c_str());
	if (result.size()>0) {
		int ID = atoi(result[0][0].c_str());
		UpdateNode(ID, Name);
		return;
	}

	m_sql.safe_query(
		"INSERT INTO DeviceStatus (HardwareID, DeviceID, Unit, Type, SubType, SwitchType, Used, SignalLevel, BatteryLevel, Name, nValue, sValue) "
		"VALUES (%d, '%q', 1, %d, %d, %d, 1, 12, 255, '%q', 0, 'Unavailable')",
		m_HwdID, PlayerID.c_str(), int(pTypeLighting2), int(sTypeAC), int(STYPE_Media), Name.c_str());		
	
	ReloadNodes();
}

void CHEOS::UpdateNode(const int ID, const std::string &Name)
{
	m_sql.safe_query("UPDATE DeviceStatus SET Name='%q' WHERE (HardwareID==%d) AND (ID=='%d')", Name.c_str(), m_HwdID, ID);	
	
	ReloadNodes();
}

void CHEOS::SetSettings(const int PollIntervalsec, const int PingTimeoutms)
{
	//Defaults
	m_iPollInterval = 30;
	m_iPingTimeoutms = 1000;

	if (PollIntervalsec > 1)
		m_iPollInterval = PollIntervalsec;
	if ((PingTimeoutms / 1000 < m_iPollInterval) && (PingTimeoutms != 0))
		m_iPingTimeoutms = PingTimeoutms;
}

void CHEOS::Restart()
{
	StopHardware();
	StartHardware();
}

void CHEOS::ReloadNodes()
{
	m_nodes.clear();
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT ID,DeviceID, Name, nValue,sValue FROM DeviceStatus WHERE (HardwareID==%d)", m_HwdID);
	if (result.size() > 0)
	{
		_log.Log(LOG_STATUS, "DENON for HEOS: %i players found.", result.size());
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt = result.begin(); itt != result.end(); ++itt)
		{
			std::vector<std::string> sd = *itt;

			HEOSNode pnode;
			pnode.ID = atoi(sd[0].c_str());
			pnode.DevID = atoi(sd[1].c_str());
			pnode.Name = sd[2];
			pnode.nStatus = (_eMediaStatus)atoi(sd[3].c_str());
			pnode.sStatus = sd[4];
			pnode.LastOK = mytime(NULL);

			m_nodes.push_back(pnode);
		}
	}
	else
	{
		_log.Log(LOG_ERROR, "DENON for HEOS: No players found.");		
	}	
}


//Webserver helpers
namespace http {
	namespace server {
		void CWebServer::Cmd_HEOSSetMode(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				//No admin user, and not allowed to be here
				return;
			}
			std::string hwid = request::findValue(&req, "idx");
			std::string mode1 = request::findValue(&req, "mode1");
			std::string mode2 = request::findValue(&req, "mode2");
			if (
				(hwid == "") ||
				(mode1 == "") ||
				(mode2 == "")
				)
				return;
			int iHardwareID = atoi(hwid.c_str());
			CDomoticzHardwareBase *pBaseHardware = m_mainworker.GetHardware(iHardwareID);
			if (pBaseHardware == NULL)
				return;
			if (pBaseHardware->HwdType != HTYPE_HEOS)
				return;
			CHEOS *pHardware = reinterpret_cast<CHEOS*>(pBaseHardware);

			root["status"] = "OK";
			root["title"] = "HEOSSetMode";

			int iMode1 = atoi(mode1.c_str());
			int iMode2 = atoi(mode2.c_str());

			m_sql.safe_query("UPDATE Hardware SET Mode1=%d, Mode2=%d WHERE (ID == '%q')", iMode1, iMode2, hwid.c_str());
			pHardware->SetSettings(iMode1, iMode2);
			pHardware->Restart();
		}

		/*
		void CWebServer::Cmd_LMSGetPlaylists(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string hwid = request::findValue(&req, "idx");
			if (hwid == "")
				return;
			int iHardwareID = atoi(hwid.c_str());
			CDomoticzHardwareBase *pBaseHardware = m_mainworker.GetHardware(iHardwareID);
			if (pBaseHardware == NULL)
				return;
			if (pBaseHardware->HwdType != HTYPE_LogitechMediaServer)
				return;
			CHEOS *pHardware = reinterpret_cast<CHEOS*>(pBaseHardware);

			root["status"] = "OK";
			root["title"] = "Cmd_LMSGetPlaylists";

			std::vector<CHEOS::LMSPlaylistNode> m_nodes = pHardware->GetPlaylists();
			std::vector<CHEOS::LMSPlaylistNode>::const_iterator itt;

			int ii = 0;
			for (itt = m_nodes.begin(); itt != m_nodes.end(); ++itt) {
				root["result"][ii]["id"] = itt->ID;
				root["result"][ii]["refid"] = itt->refID;
				root["result"][ii]["Name"] = itt->Name;
				ii++;
			}
		}
		*/
		
		void CWebServer::Cmd_HEOSMediaCommand(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string sIdx = request::findValue(&req, "idx");
			std::string sAction = request::findValue(&req, "action");
			if (sIdx.empty())
				return;
			int idx = atoi(sIdx.c_str());
			root["status"] = "OK";
			root["title"] = "HEOSMediaCommand";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT DS.SwitchType, H.Type, H.ID FROM DeviceStatus DS, Hardware H WHERE (DS.ID=='%q') AND (DS.HardwareID == H.ID)", sIdx.c_str());
			if (result.size() == 1)
			{
				_eSwitchType	sType = (_eSwitchType)atoi(result[0][0].c_str());
				_eHardwareTypes	hType = (_eHardwareTypes)atoi(result[0][1].c_str());
				int HwID = atoi(result[0][2].c_str());
				// Is the device a media Player?
				if (sType == STYPE_Media)
				{
					switch (hType) {
					case HTYPE_HEOS:
						CHEOS HEOS(HwID);
						HEOS.SendCommand(idx, sAction);
						break;
						// put other players here ...
					}
				}
			}
		}

	}
}
