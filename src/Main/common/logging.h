#ifndef CAPTURER_LOGGING_H
#define CAPTURER_LOGGING_H

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <fmt/format.h>
#include <ng-log/flags.h>
#include <ng-log/logging.h>
#include <probe/thread.h>
#include <QApplication>

class Logger
{
public:
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    static Logger& init(const char *argv0)
    {
        static Logger logger(argv0);
        return logger;
    }

    ~Logger() { nglog::ShutdownLogging(); }

private:
    explicit Logger(const char *argv0)
    {
#ifdef _WIN32
        const std::string log_dir = "./logs";
#elif __linux__
        const std::string log_dir = std::string{ ::getenv("HOME") } + "/.local/logs/capturer";
#elif __APPLE__
        QString log_path = QCoreApplication::applicationDirPath() + "/logs";
        const std::string log_dir = log_path.toStdString();
#endif
        if (!std::filesystem::is_directory(log_dir) || std::filesystem::exists(log_dir)) {
            std::filesystem::create_directories(log_dir);
        }

        // clang-format off
        nglog::InstallPrefixFormatter(
            [](std::ostream& _stream, const nglog::LogMessage& _m, void*)
            {
                std::string _file_line = std::string(_m.fullname()) + ":" + std::to_string(_m.line());
                _file_line = _file_line.substr(std::max<int64_t>(0, _file_line.size() - 24), 24);

                _stream
                    << std::setw(4) << 1900 + _m.time().year() << '-'
                    << std::setw(2) << 1 + _m.time().month() << '-'
                    << std::setw(2) << _m.time().day()
                    << ' '
                    << std::setw(2) << _m.time().hour() << ':'
                    << std::setw(2) << _m.time().min() << ':'
                    << std::setw(2) << _m.time().sec() << "."
                    << std::setw(3) << _m.time().usec() / 1000
                    << std::setfill(' ')
                    << ' '
                    << std::setw(7) << nglog::GetLogSeverityName(_m.severity())
                    << ' '
                    << std::setw(5) << _m.thread_id()
                    << " -- ["
                    << std::setw(24) << _file_line
                    << "] ["
#ifdef _WIN32
                    <<  std::setw(32) << probe::thread::name().substr(0, 32) << "]:";
#else
                    <<  std::setw(15) << probe::thread::name().substr(0, 15) << "]:";
#endif
            }
        );
        // clang-format on

        nglog::InitializeLogging(argv0);

        nglog::SetFlag(FLAGS_log_dir, log_dir);
        nglog::SetFlag(FLAGS_max_log_size, 32); // MB
        nglog::SetFlag(FLAGS_stop_logging_if_full_disk, true);
        nglog::SetFlag(FLAGS_logbufsecs, 0);  // s
        nglog::SetFlag(FLAGS_colorlogtostderr, true);

        nglog::SetStderrLogging(NGLOG_INFO);
        nglog::SetLogDestination(NGLOG_INFO, std::string{ log_dir + "/SCer-I-" }.c_str());
        nglog::SetLogDestination(NGLOG_WARNING, std::string{ log_dir + "/SCer-W-" }.c_str());
        nglog::SetLogDestination(NGLOG_ERROR, std::string{ log_dir + "/SCer-E-" }.c_str());
        nglog::SetLogDestination(NGLOG_FATAL, std::string{ log_dir + "/SCer-F-" }.c_str());
        nglog::SetLogFilenameExtension(".log");
        nglog::EnableLogCleaner(std::chrono::hours(24 * 3)); // 3 days
        nglog::InstallFailureSignalHandler();
        nglog::InstallFailureWriter(
            [](const char *data, size_t size) { LOG(ERROR) << std::string(data, size); });
    }
};

#define NOTNULL(VAL) CHECK_NOTNULL(VAL)

#define logd(FMT, ...) DLOG(INFO) << fmt::format(FMT, ##__VA_ARGS__)
#define logi(FMT, ...) LOG(INFO) << fmt::format(FMT, ##__VA_ARGS__)
#define logw(FMT, ...) LOG(WARNING) << fmt::format(FMT, ##__VA_ARGS__)
#define loge(FMT, ...) LOG(ERROR) << fmt::format(FMT, ##__VA_ARGS__)
#define logf(FMT, ...) LOG(FATAL) << fmt::format(FMT, ##__VA_ARGS__)

#define logd_if(C, FMT, ...) DLOG_IF(INFO, C) << fmt::format(FMT, ##__VA_ARGS__)
#define logi_if(C, FMT, ...) LOG_IF(INFO, C) << fmt::format(FMT, ##__VA_ARGS__)
#define logw_if(C, FMT, ...) LOG_IF(WARNING, C) << fmt::format(FMT, ##__VA_ARGS__)
#define loge_if(C, FMT, ...) LOG_IF(ERROR, C) << fmt::format(FMT, ##__VA_ARGS__)
#define logf_if(C, FMT, ...) LOG_IF(FATAL, C) << fmt::format(FMT, ##__VA_ARGS__)

#endif //! CAPTURER_LOGGING_H
