/*
 *  DedicatedControl.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 11.01.08.
 *  code under LGPL
 *
 */

// define HAVE_BOOST if you want to compile dedicated server for Win32 and have Boost headers installed.

// A workaround for a bug in boost - doesn't compile with DEBUG_NEW in MSVC 2005
#if defined(_MSC_VER) && _MSC_VER == 1400
#undef new
#endif


#ifdef WITH_PYTHON
#include <Python.h>
#endif

#include <string>
#include <sstream>
#include <stdexcept>
#include "ThreadPool.h"
#include <fcntl.h>
#include <iostream>

#include "Debug.h"
#include "LieroX.h"
#include "IpToCountryDB.h"
#include "DedicatedControl.h"
#include "FindFile.h"
#include "StringUtils.h"
#include "DeprecatedGUI/Menu.h"
#include "ProfileSystem.h"
#include "CClient.h"
#include "CServer.h"
#include "CWorm.h"
#include "CGameScript.h"
#include "Unicode.h"
#include "Protocol.h"
#include "CScriptableVars.h"
#include "CClientNetEngine.h"
#include "CChannel.h"
#include "CServerConnection.h"
#include "CServerNetEngine.h"
#include "Command.h"
#include "Process.h"


#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif


using std::endl;



static DedicatedControl* dedicatedControlInstance = NULL;

DedicatedControl* DedicatedControl::Get() 
{ 
	return dedicatedControlInstance; 
}

bool DedicatedControl::Init() {
	dedicatedControlInstance = new DedicatedControl();
	return dedicatedControlInstance->Init_priv();
}

void DedicatedControl::Uninit() {
	delete dedicatedControlInstance;
	dedicatedControlInstance = NULL;
}

static void Ded_ParseCommand(std::stringstream& s, std::string& cmd, std::string& rest) {
	cmd = ""; rest = "";

	char c;
	while( s.good() ) {
		c = s.get();
		if(c > 32) {
			cmd += c;
		} else {
			if(c == 13 || c == 10) return;
			break;
		}
	}

	while( s.good() ) {
		c = s.get();
		if(c == 13 || c == 10) return;
		rest += c;
	}
}

struct DedIntern {

	static DedIntern* Get() { return (DedIntern*)dedicatedControlInstance->internData; }


	// The old way, with pipes

	ThreadPoolItem* pipeThread;
	ThreadPoolItem* stdinThread;
	Process pipe;
	SDL_mutex* pipeOutputMutex;
	std::stringstream pipeOutput;
	bool quitSignal;

#ifdef WITH_PYTHON
	// The new way, embedded Python
	PyObject * scriptModule;
	PyObject * scriptMainLoop;
	bool ScriptSignalHandlerRecursive;
	std::ostringstream inSignals;
	static PyMethodDef DedScriptEngineMethods[3]; // Array for registerging GetSignals() and SendCommand()
	bool usePython;
#endif

	std::basic_ostream<char> & getPipe()
	{
#ifdef WITH_PYTHON
		if( usePython )
			return inSignals;
#endif
		return pipe.in();
	};
	
	void closePipe()
	{
#ifdef WITH_PYTHON
		if( scriptMainLoop )
			return;
#endif
		pipe.close();
	}

	DedIntern() :
		pipeThread(NULL), stdinThread(NULL),
		pipeOutputMutex(NULL), quitSignal(false), state(S_INACTIVE)
	{
		dedicatedControlInstance->internData = this;
		pipeOutputMutex = SDL_CreateMutex();
		stdinThread = threadPool->start(&DedIntern::stdinThreadFunc, NULL, "Ded stdin watcher");

#ifdef WITH_PYTHON
		Py_SetProgramName("python"); // Where to look for Python DLL and standard modules
		Py_Initialize();
		Py_InitModule("OLX", DedScriptEngineMethods);
		PyEval_InitThreads(); // Python-threading magic stuff, need so OLX won't crash
		scriptModule = NULL;
		scriptMainLoop = NULL;

		ScriptSignalHandlerRecursive = false;
		usePython = false;;
#endif

	}
	
	~DedIntern() {
		Sig_Quit();
		quitSignal = true;

		notes << "waiting for stdinThread ..." << endl;
		threadPool->wait(stdinThread, NULL);

		breakCurrentScript();
		
		SDL_DestroyMutex(pipeOutputMutex);

#ifdef WITH_PYTHON
		Py_XDECREF(scriptMainLoop);
		Py_XDECREF(scriptModule);
		Py_Finalize();
		usePython = false;
#endif

		notes << "DedicatedControl destroyed" << endl;
		
		dedicatedControlInstance->internData = NULL;
	}

	bool loadScript(const std::string& script) 
	{
		breakCurrentScript();
#ifdef WITH_PYTHON
		FILE * fp = OpenGameFile(script, "r");
		if( fp )
		{
			std::string fpContents = ReadUntil( fp, '\n' );
			fclose(fp);
			if( fpContents.find("python") != std::string::npos )
				return loadScript_Python(script);
		}
#endif
		return loadScript_Pipe(script);
	}

	// Pipe functions
	
	// reading lines from pipe-out and put them to pipeOutput
	static int pipeThreadFunc(void*) {
		DedIntern* data = Get();

		while(!data->pipe.out().eof()) {
			std::string buf;
			getline(data->pipe.out(), buf);

			SDL_mutexP(data->pipeOutputMutex);
			data->pipeOutput << buf << endl;
	 		SDL_mutexV(data->pipeOutputMutex);
		}
		return 0;
	}

	// reading lines from stdin and put them to pipeOutput
	static int stdinThreadFunc(void*) {
		DedIntern* data = Get();

#ifndef WIN32
		// TODO: there's no fcntl for Windows!
		if(fcntl(0, F_SETFL, O_NONBLOCK) == -1)
#endif
			warnings << "ERROR setting standard input into non-blocking mode" << endl;

		while(true) {
			std::string buf;
			while(true) {
				SDL_Delay(10); // TODO: maxfps here
				if(data->quitSignal) return 0;

				char c;

				if(read(0, &c, 1) >= 0) {
					if(c == '\n') break;
					if(c == -52) return 0;  // CTRL-C
					buf += c;
				}
			}

			SDL_mutexP(data->pipeOutputMutex);
			data->pipeOutput << buf << endl;
	 		SDL_mutexV(data->pipeOutputMutex);
		}
		return 0;
	}

	
	bool breakCurrentScript() {
#ifdef WITH_PYTHON
		if( usePython )
		{
			Py_XDECREF(scriptMainLoop);
			Py_XDECREF(scriptModule);
			scriptMainLoop = NULL;
			scriptModule = NULL;
			usePython = false;
			return true;
		}
#endif
		if(pipeThread) {
			notes << "waiting for pipeThread ..." << endl;
			pipe.close();
			threadPool->wait(pipeThread, NULL);
		}
		pipeThread = NULL;
		return true;
	}
	
	bool loadScript_Pipe(const std::string& script) {
		breakCurrentScript();
		
		std::string scriptfn = GetAbsolutePath(GetFullFileName(script));
		if(script != "/dev/null") {
			if(!IsFileAvailable(scriptfn, true)) {
				errors << "Dedicated: " << scriptfn << " not found" << endl;
				return false;
			}			

			notes << "Dedicated server: running script \"" << scriptfn << "\"" << endl;
			// HINT: If a script need this change in his own directory, it is a bug in the script.
			if(!pipe.open(scriptfn, std::vector<std::string>(), ExtractDirectory(scriptfn))) {
				errors << "cannot start dedicated server - cannot run script " << scriptfn << endl;
				return false;
			}

			pipeThread = threadPool->start(&DedIntern::pipeThreadFunc, NULL, "Ded pipe watcher");
		}
		else
			notes << "Dedicated server: not running any script" << endl;
		
		return true;
	}

#ifdef WITH_PYTHON
	// Embed Python into OLX

	static PyObject *
	GetSignals(PyObject *self, PyObject *args)
	{
		if(!PyArg_ParseTuple(args, ""))
			return Py_None;
		PyObject * ret = Py_BuildValue("s", ((DedIntern *)DedicatedControl::Get()->internData)->inSignals.str().c_str());
		((DedIntern *)DedicatedControl::Get()->internData)->inSignals.str("");
		return ret;
	}

	static PyObject *
	SendCommand(PyObject *self, PyObject *args)
	{
		const char *command = NULL;
		if (!PyArg_ParseTuple(args, "s", &command))
			return Py_None;
			
		std::string cmd, rest;
		std::stringstream os;
		os << command << "\n";
		Ded_ParseCommand(os, cmd, rest);

		((DedIntern *)DedicatedControl::Get()->internData)->HandleCommand(cmd, rest);

		return Py_None;
	}

	void ScriptSignalHandler()
	{
		if(!usePython)
			return;
			
		if( ScriptSignalHandlerRecursive )
			return;
		
		ScriptSignalHandlerRecursive = true;

		PyGILState_STATE gstate;
		gstate = PyGILState_Ensure();	// Python-threading magic stuff, need so OLX won't crash
		
		PyObject * pArgs = PyTuple_New(0);
		
		PyObject * pRet = PyObject_CallObject(scriptMainLoop, pArgs);

		if( PyErr_Occurred() )
		{
			notes << "Python exception (in stderr)" << endl;
			PyErr_Print(); // TODO: prints to stderr, dunno how to fetch string from it
			PyErr_Clear();
		}

		Py_XDECREF(pArgs);
		Py_XDECREF(pRet);

		PyGILState_Release(gstate);	// Python-threading magic stuff, need so OLX won't crash
		
		ScriptSignalHandlerRecursive = false;
	};
                     

	bool loadScript_Python(const std::string& script) 
	{
		std::string scriptfn = GetAbsolutePath(GetFullFileName(script));
		if(script != "/dev/null") {
			if(!IsFileAvailable(scriptfn, true)) {
				errors << "Dedicated: " << scriptfn << " not found" << endl;
				return false;
			}			

			notes << "Dedicated server: running script \"" << scriptfn << "\" using built-in Python" << endl;
			PyGILState_STATE gstate;
			gstate = PyGILState_Ensure();	// Python-threading magic stuff, need so OLX won't crash

			char tmp[1024];
			strcpy(tmp, scriptfn.c_str());
			char * tmp1 = tmp;
			PySys_SetArgv(1, & tmp1 );
			
			FILE * fp = OpenGameFile(scriptfn, "r");
			std::string fpContents = ReadUntil( fp, '\0' );
			fclose(fp);
			
			// This just compiles ded script, not executes it yet
			PyObject * codeObject = Py_CompileString( fpContents.c_str(), scriptfn.c_str(), Py_file_input);
			
			if( codeObject == NULL )
			{
				notes << "Dedicated server: compiling script \"" << scriptfn << "\" failed!" << endl;
				if( PyErr_Occurred() )
				{
					notes << "Python exception (in stderr)" << endl;
					PyErr_Print(); // TODO: prints to stderr, dunno how to fetch string from it
					PyErr_Clear();
				}
				return false;
				PyGILState_Release(gstate);
			}
			
			// Execute and import the module (reloads it if called second time)
			usePython = true;
			ScriptSignalHandlerRecursive = true;
			scriptModule = PyImport_ExecCodeModule("dedicated_control", codeObject);
			ScriptSignalHandlerRecursive = false;
			
			Py_XDECREF(codeObject);
			
			if( scriptModule == NULL )
			{
				notes << "Dedicated server: importing script \"" << scriptfn << "\" failed!" << endl;
				if( PyErr_Occurred() )
				{
					notes << "Python exception (in stderr)" << endl;
					PyErr_Print(); // TODO: prints to stderr, dunno how to fetch string from it
					PyErr_Clear();
				}
				usePython = false;
				PyGILState_Release(gstate);
				return false;
			}
			
			PyObject * pFunc = PyObject_GetAttrString(scriptModule, "MainLoop");
			if (pFunc && PyCallable_Check(pFunc))
				scriptMainLoop = pFunc;
			else
			{
				notes << "Dedicated server: importing script \"" << scriptfn << "\" failed - no MainLoop() function in module" << endl;
				if( PyErr_Occurred() )
				{
					notes << "Python exception (in stderr)" << endl;
					PyErr_Print(); // TODO: prints to stderr, dunno how to fetch string from it
					PyErr_Clear();
				}
				Py_XDECREF(pFunc);
				Py_XDECREF(scriptModule);
				pFunc = NULL;
				scriptModule = NULL;
				usePython = false;
				PyGILState_Release(gstate);
				return false;
			}
			PyGILState_Release(gstate);
		}
		else
			notes << "Dedicated server: not running any script" << endl;
		
		return true;
	}

#else
	void ScriptSignalHandler() {}; // stub
#endif	
	
	
	// -------------------------------
	// ------- state -----------------


	enum State {
		S_INACTIVE, // server was not started
		S_SVRLOBBY, // in lobby
		S_SVRPREPARING, // in game: just started, will go to S_SVRWEAPONS
		S_SVRWEAPONS, // in game: in weapon selection
		S_SVRPLAYING, // in game: playing
		S_CLICONNECTING, // client game: connecting right now
		S_CLILOBBY,
		S_CLIPLAYING
		};
	State state;

	// TODO: Move this?
	CWorm* CheckWorm(int id, const std::string& caller)
	{
		if(id <0 || id >= MAX_WORMS)
		{
			warnings << "DedicatedControl: " << caller << " : Faulty ID (got " << id << ")" << endl;
			return NULL;
		}
		CWorm *w = cServer->getWorms() + id;
		if(!w->isUsed())
		{
			warnings << "DedicatedControl: " << caller << " : ID not in use" << endl;
			return NULL;
		}
		return w;
	}

	// --------------------------------
	// ---- commands ------------------

	void Cmd_Quit() {
		*DeprecatedGUI::bGame = false; // this means if we were in menu => quit
		DeprecatedGUI::tMenu->bMenuRunning = false; // if we were in menu, quit menu

		tLX->bQuitGame = true; // quit main-main-loop
		SetQuitEngineFlag("DedicatedControl::Cmd_Quit()"); // quit main-game-loop
	}

	void Cmd_Message(const std::string& msg) {
		hints << "DedicatedControl: message: " << msg << endl;
	}
	
	void Cmd_Script(const std::string& script) {
		if(script == "" || script == "/dev/null")
			loadScript("/dev/null");
		else if(IsAbsolutePath(script))
			loadScript(script);
		else
			loadScript("scripts/" + script);
	}

	// adds a worm to the game (By string - id is way to complicated)
	void Cmd_AddBot(const std::string & params)
	{
		// Default botname
		// New variable so that we won't break const when we trim spaces.
		std::string localWorm = "[CPU] Kamikazee!";
		if (params != "")
		{
			localWorm = params;
			TrimSpaces(localWorm);
		}

		// try to find the requested worm or find any other worm
		profile_t *p = FindProfile(localWorm);
		if(!p) p = GetProfiles();
		for(;p;p=p->tNext)
		{
			if(p->iType == PRF_COMPUTER->toInt())
			{
				// we found a bot, so add it
				if( cClient->getNumWorms() >= MAX_WORMS )
				{
					warnings << "Too many worms!" << endl;
					return;
				}
				cClient->getLocalWormProfiles()[cClient->getNumWorms()] = p;
				cClient->setNumWorms(cClient->getNumWorms()+1);

				// TODO: this is really hacky, but currently there is no better way to do so
				// TODO: we need some function in the client + net protocol to allow adding/removing a worm to a client on-the-fly
				//cClient->ReinitLocalWorms();
				// HINT: It is not mandatory to have something new in the net protocol. We can reuse the connect-packet for that.
				cClient->Reconnect();

				return;
			}
		}

		// TODO: add a bot to profiles in that case
		warnings << "Can't find ANY bot!" << endl;
		return;
	}

	void Cmd_KillBots(const std::string & params) {
		for( int f=0; f<cClient->getNumWorms(); f++ )
			if( cClient->getWorm(f)->getType() == PRF_COMPUTER )
			{
				cServer->getWorms()[cClient->getWorm(f)->getID()].setLives(0);
				cClient->getNetEngine()->SendDeath(cClient->getWorm(f)->getID(), cClient->getWorm(f)->getID());
			}
	}

	// Kick and ban will both function using ID
	// It's up to the control-program to supply the ID
	// - if it sends a string atoi will fail at converting it to something sensible
	void Cmd_KickWorm(const std::string & params)
	{
		std::string reason = "";
		int id = -1;
		std::vector<std::string> sSplit = explode(params," ");

		if (sSplit.size() == 1)
			id = atoi(params);
		else if (sSplit.size() >= 2) {
			id = atoi(sSplit[0]);
			for(std::vector<std::string>::iterator it = sSplit.begin();it != sSplit.end(); it++) {
				if(it == sSplit.begin())
					continue;
				reason += *it;
				if (it+1 != sSplit.end())
					reason += " ";
			}
		}
		else {
			warnings << "DedicatedControl: KickWorm: Wrong syntax" << endl;
			return;
		}

		if(!CheckWorm(id, "KickWorm"))
			return;

		cServer->kickWorm(id,reason);
	}

	void Cmd_BanWorm(const std::string & params)
	{
		std::string reason = "";
		int id = -1;
		std::vector<std::string> sSplit = explode(params," ");

		if (sSplit.size() == 1)
			id = atoi(params);
		else if (sSplit.size() >= 2) {
			id = atoi(sSplit[0]);
			for(std::vector<std::string>::iterator it = sSplit.begin();it != sSplit.end(); it++) {
				if(it == sSplit.begin())
					continue;
				reason += *it;
				if (it+1 != sSplit.end())
					reason += " ";
			}
		}
		else {
			warnings << "DedicatedControl: BanWorm: Wrong syntax" << endl;
			return;
		}
		if(!CheckWorm(id, "BanWorm"))
			return;

		cServer->banWorm(id,reason);

	}

	// TODO: Add name muting, if wanted.
	void Cmd_MuteWorm(const std::string & params)
	{
		int id = -1;
		id = atoi(params);
		if(!CheckWorm(id, "MuteWorm"))
			return;

		cServer->muteWorm(id);
	}

	void Cmd_SetWormTeam(const std::string & params)
	{
		//TODO: Is this correct? Does atoi only catch the first number sequence?
		int id = -1;
		id = atoi(params);
		int team = -1;
		if( params.find(" ") != std::string::npos )
			team = atoi( params.substr( params.find(" ")+1 ) );


		CWorm *w = CheckWorm(id,"SetWormTeam");
		if (!w)
			return;

		if( team < 0 || team > 3 )
		{
			warnings << "DedicatedControl: SetWormTeam: invalid team number" << endl;
			return;
		}

		w->getLobby()->iTeam = team;
		w->setTeam(team);
		cServer->UpdateWorms();
		cServer->SendWormLobbyUpdate();
	}

	void Cmd_AuthorizeWorm(const std::string & params)
	{
		int id = -1;
		id = atoi(params);
		if(!CheckWorm(id, "AuthorizeWorm"))
			return;

		cServer->authorizeWorm(id);
	}

	// This command just fits here perfectly
	void Cmd_SetVar(const std::string& params) {
		if( params.find(" ") == std::string::npos ) {
			warnings << "DedicatedControl: SetVar: wrong params: " << params << endl;
			return;
		}
		std::string var = params.substr( 0, params.find(" ") );
		std::string value = params.substr( params.find(" ")+1 );
		TrimSpaces( var );
		TrimSpaces( value );
		// Strip quotes if they are
		if( value.size() > 1 )
			if( value[0] == '"' && value[value.size()-1] == '"' )
				value = value.substr( 1, value.size()-2 );
		ScriptVarPtr_t varptr = CScriptableVars::GetVar(var);
		if( varptr.b == NULL )
		{
			warnings << "DedicatedControl: SetVar: no var with name " << var << endl;
			notes << "Available vars:\n" << CScriptableVars::DumpVars() << endl;
			notes << "\nFor Python ded control script:\n" << endl;
			for( CScriptableVars::iterator it = CScriptableVars::begin(); it != CScriptableVars::end(); it++ )
			{
				notes << "setvar( \"" << it->first << "\", ";
				if( it->second.type == SVT_BOOL )
					notes << (int) * it->second.b;
				else if( it->second.type == SVT_INT )
					notes << * it->second.i;
				else if( it->second.type == SVT_FLOAT )
					notes << * it->second.f;
				else if( it->second.type == SVT_STRING )
					notes << "\"" << * it->second.s << "\"";
				else if( it->second.type == SVT_COLOR )
					notes << "0x" << hex(*it->second.cl);
				else
					notes << "\"\"";
				notes << ")" << endl;
			}
			return;
		}
		CScriptableVars::SetVarByString(varptr, value);

		//notes << "DedicatedControl: SetVar " << var << " = " << value << endl;

		cServer->UpdateGameLobby();
	}

	void Cmd_StartLobby(std::string param) {
		if(state != S_INACTIVE) {
			warnings << "Ded: we cannot start the lobby in current state" << endl;
			hints << "Ded: stop lobby/game if you want to restart it" << endl;
			return;
		}
		
		if( param != "" ) {
			int port = atoi(param);
			if( port )
				tLXOptions->iNetworkPort = port;
		}

		tLXOptions->tGameInfo.iMaxPlayers = MAX(tLXOptions->tGameInfo.iMaxPlayers,2);
		tLXOptions->tGameInfo.iMaxPlayers = MIN(tLXOptions->tGameInfo.iMaxPlayers,MAX_PLAYERS);

		tLX->iGameType = GME_HOST;

		cClient->Shutdown();
		cClient->Clear();

		// Start the server
		if(!cServer->StartServer()) {
			// Crappy
			errors("ERROR: Server wouldn't start\n");
			Sig_ErrorStartLobby();
			return;
		}

		// Lets connect me to the server
		if(!cClient->Initialize()) {
			// Crappy
			errors << "Client wouldn't initialize" << endl;
			Sig_ErrorStartLobby();
			return;
		}
		cClient->Connect("127.0.0.1:" + itoa(cServer->getPort()));

		tLXOptions->tGameInfo.sModDir = "MW 1.0";
		if(!CGameScript::CheckFile(tLXOptions->tGameInfo.sModDir, tLXOptions->tGameInfo.sModName)) {
			errors << "no mod for dedicated" << endl;
			// TODO..
		}

		// Get the game type
		tLXOptions->tGameInfo.iGameMode = GMT_NORMAL;

		tLXOptions->tGameInfo.sMapFile = "CastleStrike.lxl";
		tLXOptions->tGameInfo.sMapName = DeprecatedGUI::Menu_GetLevelName(tLXOptions->tGameInfo.sMapFile);

		Sig_LobbyStarted();
	}

	void Cmd_StartGame() {
		if(cServer->getNumPlayers() <= 1 && !tLXOptions->tGameInfo.features[FT_AllowEmptyGames]) {
			warnings << "DedControl: cannot start game, too few players" << endl;
			Sig_ErrorStartGame();
			return;
		}

		// Start the game
		cClient->setSpectate(false); // don't spectate; if we have added some players like bots, use them
		cServer->StartGame();	// start in dedicated mode

		// Leave the frontend
		*DeprecatedGUI::bGame = true;
		DeprecatedGUI::tMenu->bMenuRunning = false;
		tLX->iGameType = GME_HOST;
	}

	void Cmd_GotoLobby() {
		cServer->gotoLobby();
		*DeprecatedGUI::bGame = false;
		DeprecatedGUI::tMenu->bMenuRunning = true;
	}

	void Cmd_ChatMessage(const std::string& msg, int type = TXT_NOTICE) {
		cServer->SendGlobalText(msg, type);
	}

	void Cmd_PrivateMessage(const std::string& params, int type = TXT_NOTICE) {
		int id = -1;
		id = atoi(params);
		CWorm *w = CheckWorm(id, "PrivateMessage");
		if( !w || ! w->getClient() )
			return;

		std::string msg;
		if( params.find(" ") != std::string::npos )
			msg = params.substr( params.find(" ")+1 );

		w->getClient()->getNetEngine()->SendText(msg, type);
	}

	// TODO: make it send more info. No.
	void Cmd_GetWormList(const std::string& params)
	{
		int id = -1;
		if (!params.empty()) // ID specified
			id = atoi(params);

		CWorm *w = cServer->getWorms();
		for(int i=0; i < MAX_WORMS; i++, w++)
		{
			if(!w->isUsed())
				continue;

			if (id == -1)
				Sig_WormList(w);
			else if (w->getID() == id)
				Sig_WormList(w);
		}
		Sig_EndList();
	}

	void Cmd_GetComputerWormList() {
		profile_t *p = GetProfiles();
		for(;p;p=p->tNext) {
			if(p->iType == PRF_COMPUTER->toInt())
				Sig_ComputerWormList(p);
		}
		Sig_EndList();
	}

	void Cmd_GetWormIp(const std::string& params)
	{
		int id = -1;
		id = atoi(params);
		CWorm* w = CheckWorm(id, "GetWormIp");
		if (!w)
			return;

		// TODO: Perhaps we can cut out the second argument for the signal- but that would lead to the signal being much larger. Is it worth it?
		std::string str_addr;
		NetAddrToString(w->getClient()->getChannel()->getAddress(), str_addr);
		if (str_addr != "")
			Sig_WormIp(w,str_addr);
		else
			notes << "DedicatedControl: GetWormIp: str_addr == \"\"" << endl;
	}

	void Cmd_GetWormLocationInfo(const std::string& params) {
		int id = -1;
		id = atoi(params);
		CWorm* w = CheckWorm(id,"GetWormCountryInfo");
		if (!w)
			return;

		std::string str_addr;
		IpInfo info;

		NetAddrToString(w->getClient()->getChannel()->getAddress(), str_addr);
		if (str_addr != "")
		{
			info = tIpToCountryDB->GetInfoAboutIP(str_addr);
			Sig_WormLocationInfo(w,info.Continent,info.Country,info.CountryShortcut);
		}
		else
			notes << "DedicatedControl: GetWormCountryInfo: str_addr == \"\"" << endl;

	}

	void Cmd_GetWormPing(const std::string& params) {
		int id = -1;
		id = atoi(params);
		CWorm* w = CheckWorm(id, "GetWormPing");
		if (!w)
			return;

		Sig_WormPing(w,w->getClient()->getChannel()->getPing());
	}

	void Cmd_GetWormSkin(const std::string& params) {
		int id = -1;
		id = atoi(params);
		CWorm* w = CheckWorm(id, "GetWormSkin");
		if (!w)
			return;

		Sig_WormSkin(w);
	}

	void Cmd_Connect(const std::string& params) {
		JoinServer(params, params, "");
		Sig_Connecting(params);
	}


	void HandleCommand(const std::string& cmd_, const std::string& params) {
		std::string cmd = cmd_; stringlwr(cmd); TrimSpaces(cmd);
		if(cmd == "") return;

#ifdef DEBUG
		// This message just makes everything else unreadable
		// Doesn't need to be printed even in debugmode.
		if (cmd != "getwormping")
			notes << "DedicatedControl: exec: " << cmd << " " << params << endl;
#endif
		// TODO: merge these commands with ingame console commands (Commands.cpp)
		if(cmd == "quit")
			Cmd_Quit();
		else if(cmd == "setvar")
			Cmd_SetVar(params);
		else if(cmd == "script")
			Cmd_Script(params);
		else if(cmd == "msg")
			Cmd_Message(params);
		else if(cmd == "chatmsg")
			Cmd_ChatMessage(params);
		else if(cmd == "privatemsg")
			Cmd_PrivateMessage(params);
		else if(cmd == "startlobby")
			Cmd_StartLobby(params);
		else if(cmd == "startgame")
			Cmd_StartGame();
		else if(cmd == "gotolobby")
			Cmd_GotoLobby();

		else if(cmd == "addbot")
			Cmd_AddBot(params);
		else if(cmd == "killbots")
			Cmd_KillBots(params);

		else if(cmd == "kickworm")
			Cmd_KickWorm(params);
		else if(cmd == "banworm")
			Cmd_BanWorm(params);
		else if(cmd == "muteworm")
			Cmd_MuteWorm(params);

		else if(cmd == "setwormteam")
			Cmd_SetWormTeam(params);

		else if(cmd == "authorizeworm")
			Cmd_AuthorizeWorm(params);

		else if(cmd =="getwormlist")
			Cmd_GetWormList(params);
		else if(cmd == "getcomputerwormlist")
			Cmd_GetComputerWormList();
		else if(cmd == "getwormip")
			Cmd_GetWormIp(params);
		else if(cmd == "getwormlocationinfo")
			Cmd_GetWormLocationInfo(params);
		else if(cmd == "getwormping")
			Cmd_GetWormPing(params);
		else if(cmd == "getwormskin")
			Cmd_GetWormSkin(params);

		else if(cmd == "connect")
			Cmd_Connect(params);

		else if(Cmd_ParseLine(cmd + " " + params)) {}
		else
			warnings << "DedicatedControl: unknown command: " << cmd << " " << params << endl;
	}

	// ----------------------------------
	// ----------- signals --------------

	// Keep up with how THIS recieves signals - "params" are split by space, use same when sending.
	void Sig_LobbyStarted() { getPipe() << "lobbystarted" << endl; state = S_SVRLOBBY; ScriptSignalHandler(); }
	void Sig_GameLoopStart() { getPipe() << "gameloopstart" << endl; state = S_SVRPREPARING; ScriptSignalHandler(); }
	void Sig_GameLoopEnd() {
		getPipe() << "gameloopend" << endl;
		if(state != S_SVRLOBBY && state != S_CLILOBBY)
			// This is because of the current game logic: It will end the game
			// loop and then return to the lobby but only in the case if we got a
			// BackToLobby-signal before; if we didn't get such a signal and
			// the gameloop was ended, that means that the game was stopped
			// completely.
			state = S_INACTIVE;
		ScriptSignalHandler();
	}
	void Sig_WeaponSelections() { getPipe() << "weaponselections" << endl; state = S_SVRWEAPONS; ScriptSignalHandler(); }
	void Sig_GameStarted() { getPipe() << "gamestarted" << endl; state = S_SVRPLAYING; ScriptSignalHandler(); }
	void Sig_BackToLobby() { getPipe() << "backtolobby" << endl; state = S_SVRLOBBY; ScriptSignalHandler(); }
	void Sig_ErrorStartLobby() { getPipe() << "errorstartlobby" << endl; state = S_INACTIVE; ScriptSignalHandler(); }
	void Sig_ErrorStartGame() { getPipe() << "errorstartgame" << endl; ScriptSignalHandler(); }
	void Sig_Quit() { getPipe() << "quit" << endl; closePipe(); state = S_INACTIVE; ScriptSignalHandler(); }

	void Sig_Connecting(const std::string& addr) { getPipe() << "connecting " << addr << std::endl; state = S_CLICONNECTING; ScriptSignalHandler(); }
	void Sig_ConnectError(const std::string& err) { getPipe() << "connecterror " << err << std::endl; state = S_INACTIVE; ScriptSignalHandler(); }
	void Sig_Connected() { getPipe() << "connected" << std::endl; state = S_CLILOBBY; ScriptSignalHandler(); }
	void Sig_ClientError() { getPipe() << "clienterror" << std::endl; state = S_INACTIVE; ScriptSignalHandler(); }
	void Sig_ClientConnectionError(const std::string& err) { getPipe() << "connectionerror " << err << std::endl; state = S_INACTIVE; ScriptSignalHandler(); }
	void Sig_ClientGameStarted() { getPipe() << "clientgamestarted" << std::endl; state = S_CLIPLAYING; ScriptSignalHandler(); }
	void Sig_ClientGotoLobby() { getPipe() << "clientbacktolobby" << std::endl; state = S_CLILOBBY; ScriptSignalHandler(); }

	void Sig_NewWorm(CWorm* w) { getPipe() << "newworm " << w->getID() << " " << w->getName() << std::endl; ScriptSignalHandler(); }
	void Sig_WormLeft(CWorm* w) { getPipe() << "wormleft " << w->getID() << " " << w->getName() << std::endl; ScriptSignalHandler(); }
	void Sig_WormList(CWorm* w) { getPipe() << "wormlistinfo " << w->getID() << " " << w->getName() << std::endl; ScriptSignalHandler(); }
	void Sig_ComputerWormList(profile_t * w) { getPipe() << "computerwormlistinfo " << w->iID << " " << w->sName << std::endl; ScriptSignalHandler(); }
	void Sig_EndList() { getPipe() << "endlist" << std::endl; ScriptSignalHandler(); }
	void Sig_ChatMessage(CWorm* w, const std::string& message) { getPipe() << "chatmessage " << w->getID() << " " << message << std::endl; ScriptSignalHandler(); }
	void Sig_PrivateMessage(CWorm* w, CWorm* to, const std::string& message) { getPipe() << "privatemessage " << w->getID() << " " << to->getID() << " " << message << std::endl; ScriptSignalHandler(); }
	void Sig_WormDied(CWorm* died, CWorm* killer) { getPipe() << "wormdied " << died->getID() << " " << killer->getID() << std::endl; ScriptSignalHandler(); }
	void Sig_WormSpawned(CWorm* worm) { getPipe() << "wormspawned " << worm->getID() << std::endl; ScriptSignalHandler(); }
	void Sig_WormIp(CWorm* w, const std::string& ip) { getPipe() << "wormip " << w->getID() << " " << ip << std::endl; ScriptSignalHandler(); }
	// Continents don't have spaces in em.
	// TODO: Bad forward compability. We might get new continents.
	// CountryShortcuts don't have spaces in em.
	// Countries CAN have spacies in em. (United Arab Emirates for example, pro country)
	void Sig_WormLocationInfo(CWorm* w, const std::string& continent, const std::string& country, const std::string& countryShortcut) {
		getPipe() << "wormlocationinfo " << w->getID() << " " << continent << " " << countryShortcut << " " << country  << endl; ScriptSignalHandler(); }

	void Sig_WormPing(CWorm* w, int ping) {	getPipe() << "wormping " << w->getID() << " " << ping << endl; ScriptSignalHandler(); }
	void Sig_WormSkin(CWorm* w) { getPipe() << "wormskin " << w->getID() << " " << w->getSkin().getDefaultColor() << " " << w->getSkin().getFileName() << endl; ScriptSignalHandler(); }

	// TODO: Make other commands for requesting more infos from a worm. Don't spam wormlist.
	// Like some more non-game/lobby specific things (I don't know what i mean by this, perhaps you do?)
	// TODO: Send all kills/deaths/teamkills after each game? Could get ugly real fast thou.



	// ----------------------------------
	// ---------- frame handlers --------

	void Frame_ServerLobby() {
		// Process the server & client frames
		cServer->Frame();
		cClient->Frame();
	}

	void Frame_Playing() {
		// we don't have to process server/client frames here as it is done already by the main loop
	}

	void Frame_ClientConnecting() {
		cClient->Frame();

		// are we connected?
		if(cClient->getStatus() == NET_CONNECTED) {
			Sig_Connected();
			return;
		}

		// error?
		if(cClient->getBadConnection()) {
			warnings << "Bad connection: " << cClient->getBadConnectionMsg() << endl;
			Sig_ConnectError(cClient->getBadConnectionMsg());
			cClient->Shutdown();
			return;
		}
	}

	void Frame_ClientLobby() {
		// Process the client
		cClient->Frame();

		// If there is a client error, leave
		if(cClient->getClientError()) {
			Sig_ClientError();
			return;
		}

		// If we have started, leave the frontend
		if(cClient->getGameReady()) {
			// Leave the frontend
			*DeprecatedGUI::bGame = true;
			DeprecatedGUI::tMenu->bMenuRunning = false;
			tLX->iGameType = GME_JOIN;
			Sig_ClientGameStarted();
			return;
		}


		// Check if the communication link between us & server is still ok
		if(cClient->getServerError()) {
			warnings << "Client connection error: " << cClient->getServerErrorMsg() << endl;
			Sig_ClientConnectionError(cClient->getServerErrorMsg());
			return;
		}
	}

	void Frame_Basic() {

#ifdef WITH_PYTHON
		if( usePython )
		{
			static float lastTimeHandlerCalled = tLX->fCurTime;
			if( tLX->fCurTime > lastTimeHandlerCalled + 1.0f ) // Call once per second, when no signals pending
			{
				ScriptSignalHandler();
				lastTimeHandlerCalled = tLX->fCurTime;
			}
		}
		else
#endif
		{
			SDL_mutexP(pipeOutputMutex);
			while(pipeOutput.str().size() > (size_t)pipeOutput.tellg()) {
				std::string cmd, rest;
				Ded_ParseCommand(pipeOutput, cmd, rest);
				SDL_mutexV(pipeOutputMutex);

				HandleCommand(cmd, rest);
				SDL_mutexP(pipeOutputMutex);
			}
			SDL_mutexV(pipeOutputMutex);
		}

		ProcessEvents();

		// Some debugging stuff
#ifdef DEBUG
		int fps = GetFPS();
		static float lastFpsPrint = tLX->fCurTime;
		if (tLX->fCurTime - lastFpsPrint >= 20.0f)  {
			notes << "Current FPS: " << fps << endl;
			lastFpsPrint = tLX->fCurTime;
		}

		static float lastBandwidthPrint = tLX->fCurTime;
		if (tLX->fCurTime - lastBandwidthPrint >= 20.0f)  {
			// Upload and download rates
			float up = 0;
			float down = 0;
			
			// Get the rates
			if( tLX->iGameType == GME_JOIN ) {
				if(cClient->getChannel()) {
					down = cClient->getChannel()->getIncomingRate() / 1024.0f;
					up = cClient->getChannel()->getOutgoingRate() / 1024.0f;
				}
			}
			else if( tLX->iGameType == GME_HOST ) {
				down = cServer->GetDownload() / 1024.0f;
				up = cServer->GetUpload() / 1024.0f;
			}

			notes << "Current upload rate: " << up << " kB/s" << endl;
			notes << "Current download rate: " << down << " kB/s" << endl;
			lastBandwidthPrint = tLX->fCurTime;
		}
#endif


		switch(state) {
			case S_SVRLOBBY: Frame_ServerLobby(); break;
			case S_SVRPLAYING: Frame_Playing(); break;
			case S_CLICONNECTING: Frame_ClientConnecting(); break;
			case S_CLILOBBY: Frame_ClientLobby(); break;
			case S_CLIPLAYING: Frame_Playing(); break;
			default: break;
		}
	}
};


#ifdef WITH_PYTHON

PyMethodDef DedIntern::DedScriptEngineMethods[3] = {
		{ "GetSignals",  GetSignals, METH_VARARGS, "Get next signal from OLX, returns string which may contain several signals separated by newline"},
		{ "SendCommand",  SendCommand, METH_VARARGS, "Send command to OLX, as string"},
		{NULL, NULL, 0, NULL}        /* Sentinel */
	};
#endif

DedicatedControl::DedicatedControl() : internData(NULL) {}
DedicatedControl::~DedicatedControl() {	if(internData) delete (DedIntern*)internData; internData = NULL; }

bool DedicatedControl::Init_priv() {
	DedIntern* dedIntern = new DedIntern;
	if(tLXOptions->sDedicatedScript != "" && tLXOptions->sDedicatedScript != "/dev/null") {
		if(IsAbsolutePath(tLXOptions->sDedicatedScript))
			return dedIntern->loadScript(tLXOptions->sDedicatedScript);

		if(strStartsWith(tLXOptions->sDedicatedScript, "scripts/")) // old clients will use it like that
			return dedIntern->loadScript(tLXOptions->sDedicatedScript);
		
		return dedIntern->loadScript("scripts/" + tLXOptions->sDedicatedScript);
	}
	else
		return dedIntern->loadScript("/dev/null");
}


// This is the main game loop, the one that do all the simulation etc.
void DedicatedControl::GameLoopStart_Signal() { DedIntern::Get()->Sig_GameLoopStart(); }
void DedicatedControl::GameLoopEnd_Signal() { DedIntern::Get()->Sig_GameLoopEnd(); }
void DedicatedControl::BackToServerLobby_Signal() { DedIntern::Get()->Sig_BackToLobby(); }
void DedicatedControl::BackToClientLobby_Signal() { DedIntern::Get()->Sig_ClientGotoLobby(); }
void DedicatedControl::WeaponSelections_Signal() { DedIntern::Get()->Sig_WeaponSelections(); }
void DedicatedControl::GameStarted_Signal() { DedIntern::Get()->Sig_GameStarted(); }

void DedicatedControl::Menu_Frame() { DedIntern::Get()->Frame_Basic(); }
void DedicatedControl::GameLoop_Frame() { DedIntern::Get()->Frame_Basic(); }
void DedicatedControl::NewWorm_Signal(CWorm* w) { DedIntern::Get()->Sig_NewWorm(w); }
void DedicatedControl::WormLeft_Signal(CWorm* w) { DedIntern::Get()->Sig_WormLeft(w); }
void DedicatedControl::ChatMessage_Signal(CWorm* w, const std::string& message) { DedIntern::Get()->Sig_ChatMessage(w,message); }
void DedicatedControl::PrivateMessage_Signal(CWorm* w, CWorm* to, const std::string& message) { DedIntern::Get()->Sig_PrivateMessage(w,to,message); }
void DedicatedControl::WormDied_Signal(CWorm* worm, CWorm* killer) { DedIntern::Get()->Sig_WormDied(worm,killer); }
void DedicatedControl::WormSpawned_Signal(CWorm* worm){ DedIntern::Get()->Sig_WormSpawned(worm); };

