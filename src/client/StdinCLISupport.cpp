#include <SDL_thread.h>
#include "StdinCLISupport.h"
#include "ThreadPool.h"
#include "Mutex.h"
#include "OLXCommand.h"
#include "Debug.h"
#include "InputEvents.h"


static bool hasStdinCLISupport = false;

bool stdinCLIActive() { return hasStdinCLISupport; }

#ifndef HAVE_LINENOISE
Result initStdinCLISupport() {
	return "not implemented";
}

void quitStdinCLISupport() {}

StdinCLI_StdoutScope::StdinCLI_StdoutScope() {}
StdinCLI_StdoutScope::~StdinCLI_StdoutScope() {}
#else

#include <unistd.h>
#include <sys/select.h>
#include <linenoise.h>

struct StdinCmdLineIntf : CmdLineIntf {
	void pushReturnArg(const std::string& str) {
		notes << "CLI return: " << str << endl;
	}

	void finalizeReturn() {
		notes << "CLI return." << endl;
	}

	void writeMsg(const std::string& str, CmdLineMsgType type) {
		// TODO: handle type
		hints << "CLI: " << str << endl;
	}

};
StdinCmdLineIntf stdinCLI;

static Mutex stdoutMutex;
static bool stdoutInRawMode = false;
static bool quit = false;

struct LinenoiseEnvCustom : LinenoiseEnv {
	LinenoiseEnvCustom() {
		prompt = "OLX> ";
	}
	virtual ssize_t read(void* d, size_t nbyte) {
		Mutex::ScopedUnlock unlock(stdoutMutex);

		while(true) {
			struct timeval time;
			time.tv_sec = 0;
			time.tv_usec = 10 * 1000; // 10ms
			fd_set set;
			FD_ZERO(&set);
			FD_SET(fd, &set);
			int r = select(fd+1, &set, NULL, NULL, &time);
			{
				Mutex::ScopedLock lock(stdoutMutex);
				if(r > 0)
					return LinenoiseEnv::read(d, nbyte);
				if(r < 0)
					return -1;
				if(quit) return 0;
			}
		}
		return -1;
	}
};

LinenoiseEnvCustom linenoiseEnv;

StdinCLI_StdoutScope::StdinCLI_StdoutScope() {
	stdoutMutex.lock();
	if(stdoutInRawMode) {
		linenoiseEnv.eraseLine();
		linenoiseDisableRawMode(linenoiseEnv.fd);
	}
}

StdinCLI_StdoutScope::~StdinCLI_StdoutScope() {
	if(stdoutInRawMode) {
		linenoiseEnableRawMode(linenoiseEnv.fd);
		linenoiseEnv.refreshLine();
	}
	stdoutMutex.unlock();
}

void linenoiseCompletionCallbackFunc(const std::string& buf, LinenoiseCompletions* lc) {
	Mutex::ScopedUnlock unlock(stdoutMutex);

}

int handleStdin(void*) {
	while(!linenoiseEnv.hadReadError) {
		std::string buf;
		{
			Mutex::ScopedLock lock(stdoutMutex);
			if(quit) break;
			stdoutInRawMode = true;
			buf = linenoiseEnv.getNextInput();
			stdoutInRawMode = false;
		}
		if(!buf.empty()) {
			Execute( &stdinCLI, buf );
			WakeupIfNeeded();
		}
	}
	if(!quit)
		notes << "CLI read error. quit" << endl;
	return 0;
}

static SDL_Thread* stdinHandleThread = NULL;

Result initStdinCLISupport() {
	if(!isatty(STDIN_FILENO))
		return "stdin is not a terminal type device";

	if(linenoiseIsUnsupportedTerm())
		return "linenoise does not support this type of terminal";

	linenoiseSetCompletionCallback(linenoiseCompletionCallbackFunc);

	stdinHandleThread = SDL_CreateThread(handleStdin, NULL);
	if(stdinHandleThread == NULL)
		return "couldn't create thread";

	hasStdinCLISupport = true;
	return true;
}

void quitStdinCLISupport() {
	if(!hasStdinCLISupport) return;
	if(!stdinHandleThread) return;

	notes << "wait for StdinCLI quit" << endl;
	{
		Mutex::ScopedLock lock(stdoutMutex);
		quit = true;
	}
	SDL_WaitThread(stdinHandleThread, NULL);
	stdinHandleThread = NULL;
	hasStdinCLISupport = false;
}

#endif // HAVE_LINENOISE