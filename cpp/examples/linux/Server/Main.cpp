//-----------------------------------------------------------------------------
//
//	Main.cpp
//
//	Minimal application to test OpenZWave.
//
//	Creates an OpenZWave::Driver and the waits.  In Debug builds
//	you should see verbose logging to the console, which will
//	indicate that communications with the Z-Wave network are working.
//
//	Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------
// Other modifications completed by conradvassallo.com, then thomasloughlin.com
#include <unistd.h>
#include <pthread.h>
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "ValueStore.h"
#include "Value.h"
#include "ValueBool.h"
#include "ValueByte.h"
#include "ValueDecimal.h"
#include "ValueInt.h"
#include "ValueList.h"
#include "ValueShort.h"
#include "ValueString.h"

#include "Socket.h"
#include "SocketException.h"
#include "ProtocolException.h"
#include <string>
#include <iostream>
#include <stdio.h>
#include <vector>
#include <stdlib.h>
#include <sstream>
#include <iostream>
#include <stdexcept>

#include "Main.h"
using namespace OpenZWave;

static uint32 g_homeId = 0;
static bool g_initFailed = false;


extern const char *valueGenreStr(ValueID::ValueGenre);
extern ValueID::ValueGenre valueGenreNum(char const *);
extern const char *valueTypeStr(ValueID::ValueType);
extern ValueID::ValueType valueTypeNum(char const *);
extern const char *nodeBasicStr(uint8);
extern const char *cclassStr(uint8);
extern uint8 cclassNum(char const *str);
extern const char *controllerErrorStr(Driver::ControllerError err);

typedef struct {
	uint32			m_homeId;
	uint8			m_nodeId;
	//string			commandclass;
	bool			m_polled;
	list<ValueID>	m_values;
} NodeInfo;

// Value-Defintions of the different String values

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

enum Commands {Undefined = 0, AList, Device, SetNode, CtlController};
static std::map<std::string, Commands> s_mapStringValues;

void create_string_map()
{
 	s_mapStringValues["ALIST"] = AList;
	s_mapStringValues["DEVICE"] = Device;
	s_mapStringValues["SETNODE"] = SetNode;
	s_mapStringValues["CONTROLLER"] = CtlController;
}

bool SetValue(int32 home, int32 node, int32 value, string& err_message);

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------

NodeInfo* GetNodeInfo(uint32 const homeId, uint8 const nodeId) {
	for (list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		NodeInfo* nodeInfo = *it;
		if ((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
			return nodeInfo;
		}
	}

	return NULL;
}

NodeInfo* GetNodeInfo(Notification const* notification) {
	uint32 const homeId = notification->GetHomeId();
	uint8 const nodeId = notification->GetNodeId();
	return GetNodeInfo( homeId, nodeId );
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
int rspSocketInt = 0;
Socket rspSocket; 
void OnNotification(Notification const* _notification, void* _context) {
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock(&g_criticalSection);


	if (rspSocketInt != 0) {

		rspSocket << "OnNotification Notification type = 0x" << std::to_string(_notification->GetType()) << "\n";

	}

	switch(_notification->GetType()) {
		case Notification::Type_ValueAdded:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Add the new value to our list
				nodeInfo->m_values.push_back( _notification->GetValueID());
				if (rspSocketInt != 0) 
					rspSocket << "OnNotification Notification ValueAdded \n";
			}
			
			break;
		}

		case Notification::Type_ValueRemoved:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Remove the value from out list
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
					if((*it) == _notification->GetValueID()) {
						nodeInfo->m_values.erase(it);
						if (rspSocketInt != 0) 
							rspSocket << "OnNotification Notification ValueRemoved \n";
						break;
					}
				}
			}
			break;
		}

		case Notification::Type_ValueChanged:
		{
			int tmp_val;
			// One of the node values has changed
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				for (list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
                    			if ((*it) == _notification->GetValueID()) {
						if (rspSocketInt != 0) { 
							string tempstr="";
							Manager::Get()->GetValueAsString(*it,&tempstr);
							rspSocket << "OnNotification Notification ValueChanged node " << std::to_string(nodeInfo->m_nodeId) 
								<< " val <" << Manager::Get()->GetValueLabel(*it) << "> " << tempstr << "\n";
						}
                        			nodeInfo->m_values.erase(it);
                        			break;
                    			}
                		}
				nodeInfo->m_values.push_back(_notification->GetValueID());
				//Todo: clean up this update. This was a fast way to update the status
			}
			break;
		}

		case Notification::Type_Group:
		{
			// One of the node's association groups has changed
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo = nodeInfo;            // placeholder for real action
				if (rspSocketInt != 0) 
					rspSocket << "OnNotification Notification Group \n";
			}
			break;
		}

		case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo* nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			nodeInfo->m_polled = false;
			g_nodes.push_back(nodeInfo);
			if (rspSocketInt != 0) {
				rspSocket << "OnNotification Notification NodeAdded homeId=" << std::to_string(nodeInfo->m_homeId) 
					<< " nodeId=" << std::to_string(nodeInfo->m_nodeId) <<  "\n";
			}
			break;
		}

		case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;
				if ((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId )) {
					g_nodes.erase(it);
					delete nodeInfo;
					if (rspSocketInt != 0) 
						rspSocket << "OnNotification Notification NodeRemoved homeId=" << std::to_string(homeId) 
							<< " nodeId=" << std::to_string(nodeId) <<  "\n";
					break;
				}
			}
			break;
		}

		case Notification::Type_NodeEvent:
		{
			// We have received an event from the node, caused by a
			// basic_set or hail message.
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo = nodeInfo;            // placeholder for real action
				if (rspSocketInt != 0) 
					rspSocket << "OnNotification Notification NodeEvent homeId=" << std::to_string(nodeInfo->m_homeId)
					       	<< " nodeId=" << std::to_string(nodeInfo->m_nodeId) <<  "\n";
			}
			break;
		}

		case Notification::Type_PollingDisabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = false;
				if (rspSocketInt != 0) 
					rspSocket << "OnNotification Notification PollingDisabled homeId=" << std::to_string(nodeInfo->m_homeId) 
						<< " nodeId=" << std::to_string(nodeInfo->m_nodeId) <<  "\n";
			}
			break;
		}

		case Notification::Type_PollingEnabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = true;
				if (rspSocketInt != 0) 
					rspSocket << "OnNotification Notification PollingEnabled homeId=" << std::to_string(nodeInfo->m_homeId) 
						<< " nodeId=" << std::to_string(nodeInfo->m_nodeId) <<  "\n";
			}
			break;
		}

		case Notification::Type_DriverReady:
		{
			g_homeId = _notification->GetHomeId();
			if (rspSocketInt != 0) 
				rspSocket << "OnNotification Notification DriverReady homeId=" << std::to_string(g_homeId) <<  "\n";
			break;
		}

		case Notification::Type_DriverFailed:
		{
			g_initFailed = true;
			pthread_cond_broadcast(&initCond);
			if (rspSocketInt != 0) 
				rspSocket << "OnNotification Notification DriverFailed\n";
			break;
		}

		case Notification::Type_AwakeNodesQueried:
		case Notification::Type_AllNodesQueried:
		case Notification::Type_AllNodesQueriedSomeDead:
		{
			pthread_cond_broadcast(&initCond);
			if (rspSocketInt != 0) 
				rspSocket << "OnNotification Notification Queried\n";
			break;
		}

		case Notification::Type_DriverReset:
		case Notification::Type_Notification:
		case Notification::Type_NodeNaming:
		case Notification::Type_NodeProtocolInfo:
		case Notification::Type_NodeQueriesComplete:
		{
			if (rspSocketInt != 0) 
				rspSocket << "OnNotification Notification no process\n";
			break;
		}
		default:
		{
		}
	}

	pthread_mutex_unlock(&g_criticalSection);
}

/******** DOSTUFF() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/

void split(const string& s, char c, vector<string>& v) {
    string::size_type i = 0;
    string::size_type j = s.find(c);
    while (j != string::npos) {
        v.push_back(s.substr(i, j - i));
        i = ++j;
        j = s.find(c, j);
    }
	v.push_back(s.substr(i, s.length()));
}

string trim(string s) {
    return s.erase(s.find_last_not_of(" \n\r\t") + 1);
}

/*
 * web_controller_update
 * Handle controller function feedback from library.
 */

void controller_update (Driver::ControllerState cs, Driver::ControllerError err, void *ct)
{
  string s;
  string st;
  bool more = true;
 
  switch (cs) {
  case Driver::ControllerState_Normal:
    st = "Normal";
    s = "~no command in progress.";
    break;
  case Driver::ControllerState_Starting:
    st = "Starting";
    s = "~starting controller command.";
    break;
  case Driver::ControllerState_Cancel:
    st = "Cancel";
    s = "~command was cancelled.";
    more = false;
    break;
  case Driver::ControllerState_Error:
    st = "Error";
    s = "~command returned an error: ";
    more = false;
    break;
  case Driver::ControllerState_Sleeping:
    st = "Sleeping";
    s = "~device went to sleep.";
    more = false;
    break;
  case Driver::ControllerState_Waiting:
    st = "Waiting";
    s = "~waiting for a user action.";
    break;
  case Driver::ControllerState_InProgress:
    st = "InProgress";
    s = "~communicating with the other device.";
    break;
  case Driver::ControllerState_Completed:
    st = "Completed";
    s = "~command has completed successfully.";
    more = false;
    break;
  case Driver::ControllerState_Failed:
    st = "Failed";
    s = "~command has failed.";
    more = false;
    break;
  case Driver::ControllerState_NodeOK:
    st = "NodeOK";
    s = "~the node is OK.";
    more = false;
    break;
  case Driver::ControllerState_NodeFailed:
    st = "NodeFailed";
    s = "~the node has failed.";
    more = false;
    break;
  default:
    st = "unknown";
    s = "~unknown respose.";
    break;
  }
  if (err != Driver::ControllerError_None)
    s  = s + controllerErrorStr(err);

  if (rspSocketInt != 0) {
    rspSocket << "controllerstatus~" << st << s << "\n";
  }

  printf("%s\n", s.c_str());
}
  
  
template <typename T>
T lexical_cast(const std::string& s)
{
    std::stringstream ss(s);

    T result;
    if ((ss >> result).fail() || !(ss >> std::ws).eof())
    {
        throw std::runtime_error("Bad cast");
    }

    return result;
}

void *process_commands(void* arg);

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------

string port = "/dev/ttyMT1";
int main(int argc, char* argv[]) {
    pthread_mutexattr_t mutexattr;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_criticalSection, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);

    pthread_mutex_lock(&initMutex);

   	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file. If you leave it NULL
	// the log file will appear in the program's working directory.
    //KCOptions::Create("../../../../config/", "", "");
    Options::Create("./config/", "", "--SaveConfiguration=true --DumpTriggerLevel=0");
    Options::Get()->AddOptionInt( "PollInterval", 500 );
    Options::Get()->AddOptionBool( "IntervalBetweenPolls", true );
    Options::Get()->AddOptionBool("ValidateValueChanges", true);
    Options::Get()->Lock();

    Manager::Create();

    // Add a callback handler to the manager.  The second argument is a context that
    // is passed to the OnNotification method.  If the OnNotification is a method of
    // a class, the context would usually be a pointer to that class object, to
    // avoid the need for the notification handler to be a static.
    Manager::Get()->AddWatcher(OnNotification, NULL);


    // Add a Z-Wave Driver
    // Modify this line to set the correct serial port for your PC interface.
    Manager::Get()->AddDriver((argc > 1) ? argv[1] : port);
    //Manager::Get()->AddDriver( "HID Controller", Driver::ControllerInterface_Hid );

    // Now we just wait for the driver to become ready, and then write out the loaded config.
    // In a normal app, we would be handling notifications and building a UI for the user.

    pthread_cond_wait(&initCond, &initMutex);

    if (!g_initFailed) {
	
		create_string_map();
		Manager::Get()->WriteConfig(g_homeId);

		Driver::DriverData data;
		Manager::Get()->GetDriverStatistics(g_homeId, &data);

		printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.m_SOFCnt, data.m_ACKWaiting, data.m_readAborts, data.m_badChecksum);
		printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.m_readCnt, data.m_writeCnt, data.m_CANCnt, data.m_NAKCnt, data.m_ACKCnt, data.m_OOFCnt);
		printf("Dropped: %d Retries: %d\n", data.m_dropped, data.m_retries);
		printf("***************************************************** \n");
		printf("6004 ZWaveCommander Server \n");
		
		while(true) {
			try { // for all socket errors
				Socket server;
				if(!server.create()){
					throw SocketException ( "Could not create server socket." );
				}
				if(!server.bind(6004)){
					throw SocketException ( "Could not bind to port." );
				}
				if(!server.listen()){
					throw SocketException ( "Could not listen to socket." );
				}
				Socket new_sock;
				while(server.accept(new_sock)) {
					pthread_t thread;
					//std::cout << new_sock.GetSock() << endl;
					//segmentation fault here
					int thread_sock2;
					thread_sock2 = new_sock.GetSock();
					rspSocketInt = new_sock.GetSock();
					rspSocket.SetSock(rspSocketInt);
					//std::cout << thread_sock2 << endl;
					if( pthread_create( &thread , NULL ,  process_commands ,(void*) thread_sock2) < 0)
					{
						throw std::runtime_error("Unable to create thread");
					}
					else
					{
						std::cout<< "Connection Created" << endl;
						
					}
				}
			}
			catch (SocketException& e) {
				std::cout << "SocketException: " << e.what() << endl;
			}
			catch(...) {
				std::cout << "Other exception" << endl;
			}
			std::cout << "Caught an exception, resolve the issue and press ENTER to continue" << endl;
			std::cin.ignore();
		}
    }
	
	// program exit (clean up)
	std::cout << "Closing connection to Zwave Controller" << endl;
	
	if(strcasecmp(port.c_str(), "usb") == 0) {
		Manager::Get()->RemoveDriver("HID Controller");
	}
	else {
		Manager::Get()->RemoveDriver(port);
	}
	Manager::Get()->RemoveWatcher(OnNotification, NULL);
	Manager::Destroy();
	Options::Destroy();
	pthread_mutex_destroy(&g_criticalSection);
	return 0;
}

void *process_commands(void* arg)
{

	Socket thread_sock;
	thread_sock.SetSock((int)arg);
	while(true) {
		try { // command parsing errors
			//get commands from the socket
			std::string data;
			thread_sock >> data;
			std::cout << data << endl;
			if(strcmp(data.c_str(), "") == 0){ //client closed the connection
				std::cout << "Client closed the connection" << endl;
				return 0;
			}
			
			vector<string> v;
			split(data, '~', v);
			switch (s_mapStringValues[trim(v[0].c_str())])
			{
				case AList:
				{
					string device;
					for (list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
						NodeInfo* nodeInfo = *it;
						int nodeID = nodeInfo->m_nodeId;
						//This refreshed node could be cleaned up - I added the quick hack so I would get status
						// or state changes that happened on the switch itself or due to another event / [rpcess
						bool isRePolled= Manager::Get()->RefreshNodeInfo(g_homeId, nodeInfo->m_nodeId);
						string nodeType = Manager::Get()->GetNodeType(g_homeId, nodeInfo->m_nodeId);
						string nodeName = Manager::Get()->GetNodeName(g_homeId, nodeInfo->m_nodeId);
						string nodeZone = Manager::Get()->GetNodeLocation(g_homeId, nodeInfo->m_nodeId);
						string nodeValue ="";	//(string) Manager::Get()->RequestNodeState(g_homeId, nodeInfo->m_nodeId);
												//The point of this was to help me figure out what the node values looked like
						for (list<ValueID>::iterator it5 = nodeInfo->m_values.begin(); it5 != nodeInfo->m_values.end(); ++it5) {
							string tempstr="";
							Manager::Get()->GetValueAsString(*it5,&tempstr);                   
							tempstr= "="+tempstr;
							//hack to delimit values .. need to properly escape all values
							nodeValue+="<>"+ Manager::Get()->GetValueLabel(*it5) +tempstr;
						}

						if (nodeName.size() == 0) nodeName = "Undefined";

						if (nodeType != "Static PC Controller"  && nodeType != "") {
							stringstream ssNodeName, ssNodeId, ssNodeType, ssNodeZone, ssNodeValue;
							ssNodeName << nodeName;
							ssNodeId << nodeID;
							ssNodeType << nodeType;
							ssNodeZone << nodeZone;
							ssNodeValue << nodeValue;
							device += "DEVICE~" + ssNodeName.str() + "~" + ssNodeId.str() + "~"+ ssNodeZone.str() +"~" + ssNodeType.str() + "~" + ssNodeValue.str() + "#";
						}	
					}
					device = device.substr(0, device.size() - 1) + "\n";
					printf("device = %s\n",device.c_str());					
					printf("Sent Device List \n");
					thread_sock << "CMD&ALIST result: "  << device;
					break;
				}
				
				case Device:
				{

					if(v.size() != 4) {
					//	throw ProtocolException(2, "Wrong number of arguments");
					std::cout << v.size() << endl;
					}
					
					int Node = 0;
					int Level = 0;
					string Option = "";
					string err_message = "";

					Level = lexical_cast<int>(v[2].c_str());
					Node = lexical_cast<int>(v[1].c_str());
					Option=v[3].c_str();
					
					if(!SetValue(g_homeId, Node, Level, err_message)){
						thread_sock << "CMD&DEVICE result: error " << err_message;
					}
					else{
						stringstream ssNode, ssLevel;
						ssNode << Node;
						ssLevel << Level;
						string result = "MSG~ZWave Node=" + ssNode.str() + " Level=" + ssLevel.str() + "\n";
						thread_sock <<"CMD&DEVICE result: " << result;
					}
					break;
				}
				
				case SetNode:
				{
					if(v.size() != 4) {
					//	throw ProtocolException(2, "Wrong number of arguments");
					}
					int Node = 0;
					string NodeName = "";
					string NodeZone = "";
					
					Node = lexical_cast<int>(v[1].c_str());
					NodeName = v[2].c_str();
					NodeName = trim(NodeName);
					NodeZone = v[3].c_str();
					
					pthread_mutex_lock(&g_criticalSection);
					Manager::Get()->SetNodeName(g_homeId, Node, NodeName);
					Manager::Get()->SetNodeLocation(g_homeId, Node, NodeZone);
					pthread_mutex_unlock(&g_criticalSection);
					
					stringstream ssNode, ssName, ssZone;
					ssNode << Node;
					ssName << NodeName;
					ssZone << NodeZone;
					string result = "MSG~ZWave Name set Node=" + ssNode.str() + " Name=" + ssName.str() + " Zone=" + ssZone.str() + "\n";
					thread_sock << "CMD&SETNODE result: " << result;
					
					//save details to XML
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				
				case CtlController:
				{
					string subcommand="";	

				    	subcommand = v[1].c_str();
				    	subcommand = trim(subcommand);


				    	printf("SubCommand = %s\n", subcommand.c_str());

				    	if (subcommand == "cancel") {
					
						Manager::Get()->CancelControllerCommand(g_homeId);
						string result = "MSG~Zwave Controoler cancel cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "addd") {
				

						printf("addd\n");	
						Manager::Get()->BeginControllerCommand(g_homeId,
							    Driver::ControllerCommand_AddDevice,
							    controller_update ,NULL, true);
						string result = "MSG~Zwave Controoler addd cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "cprim") {
					
						Manager::Get()->BeginControllerCommand(g_homeId,
							    Driver::ControllerCommand_CreateNewPrimary,
							    controller_update, NULL, true);
						string result = "MSG~Zwave Controoler cprim cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "rconf") {
					
						Manager::Get()->BeginControllerCommand(g_homeId,
							    Driver::ControllerCommand_ReceiveConfiguration,
							    controller_update, NULL, true);
						string result = "MSG~Zwave Controoler rconf cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "remd") {
					
						Manager::Get()->BeginControllerCommand(g_homeId,
							    Driver::ControllerCommand_RemoveDevice,
							    controller_update, NULL, true);
						string result = "MSG~Zwave Controoler remd cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "reset") {
					
						Manager::Get()->ResetController(g_homeId);
						string result = "MSG~Zwave Controoler reset cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "sreset") {
					
						Manager::Get()->SoftReset(g_homeId);
						string result = "MSG~Zwave Controoler sreset cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;
				    	
					} else if (subcommand == "exit") {
					 
						Manager::Get()->RemoveDriver(port);
						string result = "MSG~Zwave Controoler exit cmd~Done\n";
						thread_sock << "CMD&CONTROLLER result: " << result;

					}

					break;
				}
				default:
					throw ProtocolException(1, "Unknown command");
					break;
			}
		}
		catch (ProtocolException& e) {
			string what = "ProtocolException: ";
			what += e.what();
			what += "\n";
			thread_sock << what;
		}
		catch (std::exception const& e) {
			std::cout << "Exception: " << e.what() << endl;
		}
		catch (SocketException& e) {
			std::cout << "SocketException: " << e.what() << endl;
		}
	}
}

bool SetValue(int32 home, int32 node, int32 value, string& err_message)
{
	err_message = "";
	bool bool_value;
	int int_value;
	uint8 uint8_value;
	uint16 uint16_value;
	bool response;
	bool cmdfound = false;
	
	if ( NodeInfo* nodeInfo = GetNodeInfo( home, node ) )
	{
		// Find the correct instance
		for ( list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it )
		{
			int id = (*it).GetCommandClassId();
			//int inst = (*it).GetInstance();
			string label = Manager::Get()->GetValueLabel( (*it) );
			//printf("label = %s \n", label.c_str());
			
			switch ( id )
			{

				case COMMAND_CLASS_SWITCH_BINARY:
				{
					// label="Switch" is mandatory, else it isn't a switch
					if ( label == "Switch" )
					{
						// If it is a binary CommandClass, then we only allow 0 (off) or 255 (on)
						if ( value > 0 && value < 255 )
						{
							continue;
						}
					}

					break;
				}
				case COMMAND_CLASS_SWITCH_MULTILEVEL:
				{
					// label="Level" is mandatory, else it isn't a dimmer type device
					if ( label != "Level" )
					{
						continue;
					}
					break;
				}
				case COMMAND_CLASS_LOCK:
				case COMMAND_CLASS_DOOR_LOCK:
				case COMMAND_CLASS_SCHEDULE_ENTRY_LOCK:
				{
					if (label != "Locked") {
						continue;
					}
					printf("Found DoorLock or Lock class\n");
					break;
				}
				default:
				{
					continue;
				}
			}

			if ( ValueID::ValueType_Bool == (*it).GetType() )
			{
				bool_value = (bool)value;
				response = Manager::Get()->SetValue( *it, bool_value );
				cmdfound = true;
			}
			else if ( ValueID::ValueType_Byte == (*it).GetType() )
			{
				uint8_value = (uint8)value;
				response = Manager::Get()->SetValue( *it, uint8_value );
				cmdfound = true;
			}
			else if ( ValueID::ValueType_Short == (*it).GetType() )
			{
				uint16_value = (uint16)value;
				response = Manager::Get()->SetValue( *it, uint16_value );
				cmdfound = true;
			}
			else if ( ValueID::ValueType_Int == (*it).GetType() )
			{
				int_value = value;
				response = Manager::Get()->SetValue( *it, int_value );
				cmdfound = true;
			}
			else if ( ValueID::ValueType_List == (*it).GetType() )
			{
				response = Manager::Get()->SetValue( *it, value );
				cmdfound = true;
			}
			else
			{
				//WriteLog(LogLevel_Debug, false, "Return=false (unknown ValueType)");
				printf("unknown valueType\n");
				err_message += "unknown ValueType | ";
				return false;
			}
		}


		if ( cmdfound == false )
		{
			//WriteLog( LogLevel_Debug, false, "Value=%d", value );
			//WriteLog( LogLevel_Debug, false, "Error=Couldn't match node to the required COMMAND_CLASS_SWITCH_BINARY or COMMAND_CLASS_SWITCH_MULTILEVEL");
			err_message += "Couldn't match node to the required COMMAND_CLASS_SWITCH_BINARY or COMMAND_CLASS_SWITCH_MULTILEVEL COMMAND_CLASS_SECURITY | ";
			return false;
		}

	}
	else
	{
		//WriteLog( LogLevel_Debug, false, "Return=false (node doesn't exist)" );
		err_message += "node doesn't exist";
		response = false;
	}

	return response;
}
