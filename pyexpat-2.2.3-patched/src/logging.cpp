// -*- symbian-c++ -*-

#include <flogger.h> // flogger.lib required, Symbian 7.0-up

static void Log(const TDesC8& aText)
	{
	RFileLogger log;
	TInt error = log.Connect();
	if (error)
		{
		return;
		}
	_LIT(KLogFileDir, "pyexpat");
	_LIT(KLogFileName, "pyexpat.txt");
	log.CreateLog(KLogFileDir, KLogFileName, 
				  EFileLoggingModeOverwrite);
	log.Write(aText);
	log.Close();
	}

extern "C" void Log(const char* aText)
	{
	TPtrC8 ptr(reinterpret_cast<const TUint8*>(aText));
	Log(ptr);
	}
