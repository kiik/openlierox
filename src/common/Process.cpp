/*
 *  Process.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 10.02.09.
 *  code under LGPL
 *
 */

#include <cassert>
#include "Process.h"
#include "Debug.h"
#include "FindFile.h"

#ifdef WIN32

#include <windows.h>
#include <streambuf>
#include <istream>
#include <ostream>
#include <cstddef>

namespace {

// Write-only streambuf over a Windows pipe handle (the child's stdin).
class PipeWriteBuf : public std::streambuf
{
public:
	PipeWriteBuf(HANDLE h) : m_handle(h) { setp(m_buf, m_buf + sizeof(m_buf)); }
protected:
	virtual int_type overflow(int_type c)
	{
		if(!flushBuffer()) return traits_type::eof();
		if(!traits_type::eq_int_type(c, traits_type::eof())) {
			*pptr() = traits_type::to_char_type(c);
			pbump(1);
		}
		return traits_type::not_eof(c);
	}
	virtual int sync() { return flushBuffer() ? 0 : -1; }
private:
	bool flushBuffer()
	{
		if(m_handle == INVALID_HANDLE_VALUE) return false;
		std::ptrdiff_t n = pptr() - pbase();
		char* p = pbase();
		while(n > 0) {
			DWORD written = 0;
			if(!WriteFile(m_handle, p, (DWORD)n, &written, NULL) || written == 0)
				return false;
			p += written; n -= (std::ptrdiff_t)written;
		}
		setp(m_buf, m_buf + sizeof(m_buf));
		return true;
	}
	HANDLE m_handle;
	char m_buf[4096];
};

// Read-only streambuf over a Windows pipe handle (the child's stdout).
class PipeReadBuf : public std::streambuf
{
public:
	PipeReadBuf(HANDLE h) : m_handle(h) { setg(m_buf, m_buf, m_buf); }
protected:
	virtual int_type underflow()
	{
		if(gptr() < egptr())
			return traits_type::to_int_type(*gptr());
		if(m_handle == INVALID_HANDLE_VALUE) return traits_type::eof();
		DWORD n = 0;
		if(!ReadFile(m_handle, m_buf, sizeof(m_buf), &n, NULL) || n == 0)
			return traits_type::eof();
		setg(m_buf, m_buf, m_buf + n);
		return traits_type::to_int_type(*gptr());
	}
private:
	HANDLE m_handle;
	char m_buf[4096];
};

// Quote an argument vector into a single command line following the rules
// CommandLineToArgvW uses to split it again.
static std::string buildCommandLine(const std::vector<std::string>& params)
{
	std::string out;
	for(size_t i = 0; i < params.size(); ++i) {
		if(i) out += ' ';
		const std::string& a = params[i];
		bool needQuote = a.empty() || a.find_first_of(" \t\"") != std::string::npos;
		if(!needQuote) { out += a; continue; }
		out += '"';
		size_t backslashes = 0;
		for(size_t j = 0; j < a.size(); ++j) {
			char c = a[j];
			if(c == '\\') { ++backslashes; out += c; }
			else if(c == '"') {
				out.append(backslashes + 1, '\\'); // escape the run of backslashes and the quote
				out += '"';
				backslashes = 0;
			}
			else { backslashes = 0; out += c; }
		}
		out.append(backslashes, '\\'); // double the backslashes preceding the closing quote
		out += '"';
	}
	return out;
}

} // namespace

struct ProcessIntern
{
	ProcessIntern()
		: m_stdinWr(INVALID_HANDLE_VALUE), m_stdoutRd(INVALID_HANDLE_VALUE),
		  m_process(NULL), m_inBuf(NULL), m_outBuf(NULL), m_in(NULL), m_out(NULL) {}
	~ProcessIntern() { reset(); }

	std::ostream& in() { assert(m_in != NULL); return *m_in; }
	std::istream& out() { assert(m_out != NULL); return *m_out; }

	// Closing the write end signals EOF to the child's stdin.
	void close()
	{
		if(m_in) m_in->flush();
		if(m_stdinWr != INVALID_HANDLE_VALUE) {
			CloseHandle(m_stdinWr);
			m_stdinWr = INVALID_HANDLE_VALUE;
		}
	}

	bool open( const std::string & cmd, std::vector< std::string > params, const std::string& working_dir )
	{
		reset();

		for(std::vector<std::string>::iterator it = params.begin(); it != params.end(); ++it)
			*it = Utf8ToSystemNative(*it);

		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		sa.lpSecurityDescriptor = NULL;

		HANDLE stdinRd = INVALID_HANDLE_VALUE, stdinWr = INVALID_HANDLE_VALUE;
		HANDLE stdoutRd = INVALID_HANDLE_VALUE, stdoutWr = INVALID_HANDLE_VALUE;

		if(!CreatePipe(&stdinRd, &stdinWr, &sa, 0)) {
			errors << "Process: failed to create stdin pipe" << endl;
			return false;
		}
		if(!CreatePipe(&stdoutRd, &stdoutWr, &sa, 0)) {
			errors << "Process: failed to create stdout pipe" << endl;
			CloseHandle(stdinRd); CloseHandle(stdinWr);
			return false;
		}

		// The parent-side ends must not leak into the child.
		SetHandleInformation(stdinWr, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(stdoutRd, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOA si;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = stdinRd;
		si.hStdOutput = stdoutWr;
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE); // forward the child's stderr to ours

		PROCESS_INFORMATION pi;
		ZeroMemory(&pi, sizeof(pi));

		std::string cmdline = buildCommandLine(params);
		std::vector<char> cmdlineBuf(cmdline.begin(), cmdline.end());
		cmdlineBuf.push_back('\0'); // CreateProcessA may write into this buffer

		std::string wd = Utf8ToSystemNative(working_dir);
		const char* wdp = wd.empty() ? NULL : wd.c_str();

		BOOL ok = CreateProcessA(
			NULL,             // module name is taken from the command line (allows PATH lookup)
			&cmdlineBuf[0],
			NULL, NULL,
			TRUE,             // inherit handles
			0,
			NULL,             // inherit environment
			wdp,
			&si, &pi);

		// The child holds its own copies now; drop ours whether or not it launched.
		CloseHandle(stdinRd);
		CloseHandle(stdoutWr);

		if(!ok) {
			errors << "Error running command " << cmd << " : CreateProcess failed (" << (int)GetLastError() << ")" << endl;
			CloseHandle(stdinWr);
			CloseHandle(stdoutRd);
			return false;
		}

		CloseHandle(pi.hThread);
		m_process = pi.hProcess;
		m_stdinWr = stdinWr;
		m_stdoutRd = stdoutRd;

		m_inBuf = new PipeWriteBuf(m_stdinWr);
		m_outBuf = new PipeReadBuf(m_stdoutRd);
		m_in = new std::ostream(m_inBuf);
		m_out = new std::istream(m_outBuf);
		return true;
	}

private:
	void reset()
	{
		close();
		if(m_stdoutRd != INVALID_HANDLE_VALUE) { CloseHandle(m_stdoutRd); m_stdoutRd = INVALID_HANDLE_VALUE; }
		if(m_process) { CloseHandle(m_process); m_process = NULL; }
		delete m_in; m_in = NULL;
		delete m_out; m_out = NULL;
		delete m_inBuf; m_inBuf = NULL;
		delete m_outBuf; m_outBuf = NULL;
	}

	HANDLE m_stdinWr;   // parent writes -> child stdin
	HANDLE m_stdoutRd;  // parent reads  <- child stdout
	HANDLE m_process;
	PipeWriteBuf* m_inBuf;
	PipeReadBuf* m_outBuf;
	std::ostream* m_in;
	std::istream* m_out;
};

#else
#include <pstream.h>
struct ProcessIntern
{
	ProcessIntern() : p(NULL) {}
	~ProcessIntern() { close(); reset(); }
	redi::pstream* p;
	std::ostream & in() { return *p; };
	std::istream & out() { return p->out(); };
	void reset() { if(p) delete p; p = NULL; }
	void close() {
		// p << redi::peof;
		if(!p)
			return;
		if(p->rdbuf()) p->rdbuf()->kill();
		if(p->rdbuf()) p->rdbuf()->kill(SIGKILL);
	}
	bool open( const std::string & cmd, std::vector< std::string > params, const std::string& working_dir )
	{
		reset(); p = new redi::pstream();
		p->open( cmd, params, redi::pstreams::pstdin | redi::pstreams::pstdout, working_dir ); // we don't grap the stderr, it should directly be forwarded to console
		return p->rdbuf()->error() == 0;
	}
};
#endif


Process::Process() {
	data = new ProcessIntern();
}

Process::~Process() {
	assert(data != NULL);
	delete data;
	data = NULL;
}


std::ostream& Process::in() { return data->in(); }
std::istream& Process::out() { return data->out(); }
void Process::close() { data->close(); }

#ifdef WIN32
struct SysCommand {
	std::string exec;
	std::vector<std::string> params;
};

static SysCommand GetExecForScriptInterpreter(const std::string& interpreter) {
	SysCommand ret;
	std::string& command = ret.exec;
	std::vector<std::string>& commandArgs = ret.params;
	
	std::string cmdPathRegKey = "";
	std::string cmdPathRegValue = "";
	// TODO: move that out to an own function!
	if( interpreter == "python" )
	{
		// TODO: move that out to an own function!
		command = "python.exe";			
		commandArgs.clear();
		commandArgs.push_back(command);
		commandArgs.push_back("-u");
		cmdPathRegKey = "SOFTWARE\\Python\\PythonCore\\2.5\\InstallPath";
	}
	else if( interpreter == "bash" )
	{
		// TODO: move that out to an own function!
		command = "bash.exe";
		commandArgs.clear();
		commandArgs.push_back(command);
		//commandArgs.push_back("-l");	// Not needed for Cygwin
		commandArgs.push_back("-c");
		cmdPathRegKey = "SOFTWARE\\Cygnus Solutions\\Cygwin\\mounts v2\\/usr/bin";
		cmdPathRegValue = "native";
	}
	else if( interpreter == "php" )
	{
		// TODO: move that out to an own function!
		command = "php.exe";
		commandArgs.clear();
		commandArgs.push_back(command);
		commandArgs.push_back("-f");
		cmdPathRegKey = "SOFTWARE\\PHP";
		cmdPathRegValue = "InstallDir";
	}
	else
	{
		command = interpreter + ".exe";
	}
	
	// TODO: move that out to an own function!
	if( cmdPathRegKey != "" )
	{
		HKEY hKey;
		LONG returnStatus;
		DWORD dwType=REG_SZ;
		char lszCmdPath[256]="";
		DWORD dwSize=255;
		returnStatus = RegOpenKeyEx(HKEY_LOCAL_MACHINE, cmdPathRegKey.c_str(), 0L,  KEY_READ, &hKey);
		if (returnStatus != ERROR_SUCCESS)
		{
			errors << "registry key " << cmdPathRegKey << "\\" << cmdPathRegValue << " not found - make sure interpreter is installed" << endl;
			lszCmdPath[0] = '\0'; // Perhaps it is installed in PATH
		}
		returnStatus = RegQueryValueEx(hKey, cmdPathRegValue.c_str(), NULL, &dwType,(LPBYTE)lszCmdPath, &dwSize);
		RegCloseKey(hKey);
		if (returnStatus != ERROR_SUCCESS)
		{
			errors << "registry key " << cmdPathRegKey << "\\" << cmdPathRegValue << " could not be read - make sure interpreter is installed" << endl;
			lszCmdPath[0] = '\0'; // Perhaps it is installed in PATH
		}
		
		// Add trailing slash if needed
		std::string path(lszCmdPath);
		if (path.size())  {
			if (*path.rbegin() != '\\' && *path.rbegin() != '/')
				path += '\\';
		}
		command = std::string(lszCmdPath) + command;
		commandArgs[0] = command;
	}
	
	return ret;
}
#endif

bool Process::open( const std::string & cmd, std::vector< std::string > params, const std::string& working_dir ) {
	if(params.size() == 0)
		params.push_back(cmd);
	
	std::string realcmd = cmd;
#ifdef WIN32
	std::string interpreter = GetScriptInterpreterCommandForFile(cmd);
	if(interpreter != "") {
		size_t f = interpreter.find(" ");
		if(f != std::string::npos) interpreter.erase(f);
		interpreter = GetBaseFilename(interpreter);
		SysCommand newcmd = GetExecForScriptInterpreter(interpreter);
		realcmd = newcmd.exec;
		params.swap( newcmd.params );
		params.reserve( params.size() + newcmd.params.size() );
		for(std::vector<std::string>::iterator i = newcmd.params.begin(); i != newcmd.params.end(); ++i)
			params.push_back(*i);

		notes << "running \"" << realcmd << "\" for script \"" << cmd << "\"" << endl;
	}
#endif
	
	return data->open( realcmd, params, working_dir );
}

