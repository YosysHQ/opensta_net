#ifndef OPENSTA_NET_H
#define OPENSTA_NET_H

#include "kernel/yosys.h"

#include <tcl.h>

#include "sta/ReportTcl.hh"
#include "sta/Sta.hh"

namespace opensta_net {

using Yosys::RTLIL::Design;

// Feeds a message to every log sink but the console, which the Tcl channel
// serves, so STA and Tcl output keep their order there
struct NonConsoleTee {
	const Yosys::LogMessage &msg;
	void operator()(Yosys::LogSink &sink) const;
};

struct YosysStaReport : public sta::ReportTcl {
	size_t printString(const char *buffer, size_t length) override;
};

struct YosysSta : public sta::Sta {
	Yosys::pool<std::string> liberty_files_read;
public:
	void makeReport() override;
};

struct Options {
	std::string eval_cmd, liberty_file, source_file, sdc_file;
};

// Tcl
bool tclEval(const std::string &script);

// Boot
void makeSta();
void bootSta();

// opensta_net
size_t parseOptions(const std::vector<std::string> &args, Options &opt);
void readLiberty(const std::string &file);
bool sourceFile(const std::string &file, bool sdc);
void checkSourced(bool ok, const std::string &file);
std::string unquote(const std::string &arg);
void evalTcl(const std::string &cmd);

struct OpenstaNetPass : public Yosys::Pass {
	OpenstaNetPass();
	void help() override;
	void execute(std::vector<std::string> args, Design *design) override;
};

} // namespace opensta_net

#endif
