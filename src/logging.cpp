#include "logging.h"
#include "overlay.h"
#include "config.h"
#include "battery.h"
#include <sstream>
#include <iomanip>

string os, cpu, gpu, ram, kernel, driver, cpu_governor;
bool sysInfoFetched = false;
double fps;
uint64_t frametime;
logData currentLogData = {};
ofstream currentLogFile;
std::unique_ptr<Logger> logger;

string exec(string command) {
   char buffer[128];
   string result = "";
#ifdef __gnu_linux__

   // Open pipe to file
   FILE* pipe = popen(command.c_str(), "r");
   if (!pipe) {
      return "popen failed!";
   }

   // read till end of process:
   while (!feof(pipe)) {

      // use buffer to read and add to result
      if (fgets(buffer, 128, pipe) != NULL)
         result += buffer;
   }

   pclose(pipe);
#endif
   return result;
}

void upload_file(std::string logFile){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  command += " -F 'log[uploads][]=@" + logFile + "'";

  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  exec("xdg-open " + url);
}

void upload_files(const std::vector<std::string>& logFiles){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  for (auto& file : logFiles)
    command += " -F 'log[uploads][]=@" + file + "'";

  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  exec("xdg-open " + url);
}

void writeFile(string filename){
  auto& logArray = logger->get_log_data();
#ifndef NDEBUG
  std::cerr << "Writing log file [" << filename << "], " << logArray.size() << " entries\n";
#endif
  std::ofstream out(filename, ios::out | ios::app);
  if (out){
    out << "v1" << endl;
    out << MANGOHUD_VERSION << endl;
    out << "---------------------SYSTEM INFO---------------------" << endl;
    out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver," << "cpu_governor," << "Wine/Proton," << "sync," << "renderer," << "cpu_scheduler" << endl;
    out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << "," << cpu_governor << "," << wineVersion << "," << HUDElements.sync << "," << HUDElements.sw_stats->engineName << HUDElements.cpu_sched << endl;
    out << "--------------------FRAME METRICS--------------------" << endl;
    out << "fps," << "frametime," << "cpu_load," << "gpu_load," << "cpu_temp," << "gpu_temp," << "gpu_core_clock," << "gpu_mem_clock," << "gpu_vram_used," << "gpu_power," << "ram_used," << "current_watt,";
    out << "elapsed" << endl;
    for (size_t i = 0; i < logArray.size(); i++){
      out << logArray[i].fps << ",";
      out << logArray[i].frametime / 1000.f << ",";
      out << logArray[i].cpu_load << ",";
      out << logArray[i].gpu_load << ",";
      out << logArray[i].cpu_temp << ",";
      out << logArray[i].gpu_temp << ",";
      out << logArray[i].gpu_core_clock << ",";
      out << logArray[i].gpu_mem_clock << ",";
      out << logArray[i].gpu_vram_used << ",";
      out << logArray[i].gpu_power << ",";
      out << logArray[i].ram_used << ",";
      out << logArray[i].current_watt << ",";
      out << std::chrono::duration_cast<std::chrono::nanoseconds>(logArray[i].previous).count() << "\n";
    }
  logger->clear_log_data();
  } else {
    printf("MANGOHUD: Failed to write log file\n");
  }
}

void writeFileContinuous(ofstream& out){
  auto& logArray = logger->get_log_data();
  if (out && !logArray.empty()){
    out << logArray.back().fps << ",";
    out << logArray.back().frametime / 1000.f << ",";
    out << logArray.back().cpu_load << ",";
    out << logArray.back().gpu_load << ",";
    out << logArray.back().cpu_temp << ",";
    out << logArray.back().gpu_temp << ",";
    out << logArray.back().gpu_core_clock << ",";
    out << logArray.back().gpu_mem_clock << ",";
    out << logArray.back().gpu_vram_used << ",";
    out << logArray.back().gpu_power << ",";
    out << logArray.back().ram_used << ",";
    out << logArray.back().current_watt << ",";
    for (const CPUData &cpuData : cpuStats.GetCPUData()){
      out << cpuData.percent << ",";
    }
    out << std::chrono::duration_cast<std::chrono::nanoseconds>(logArray.back().previous).count() << "\n";
  } else {
    printf("MANGOHUD: Failed to write log file\n");
  }
}

string get_log_suffix(){
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
  std::ostringstream buffer;
  buffer << std::put_time(log_time, "%Y-%m-%d_%H-%M-%S") << ".csv";
  string log_name = buffer.str();
  return log_name;
}

void logging(void *params_void){
  overlay_params *params = reinterpret_cast<overlay_params *>(params_void);
  logger->wait_until_data_valid();
  while (logger->is_active()){
      logger->try_log();
      this_thread::sleep_for(chrono::milliseconds(params->log_interval));
  }
}

Logger::Logger(overlay_params* in_params)
  : m_logging_on(false),
    m_values_valid(false),
    m_params(in_params)
{
#ifndef NDEBUG
  std::cerr << "Logger constructed!\n";
#endif
}

void Logger::start_logging() {
  if(m_logging_on) return;
  m_values_valid = false;
  m_logging_on = true;
  m_log_start = Clock::now();
  m_log_files.emplace_back(m_params->output_folder + "/" + get_program_name() + "_" + get_log_suffix());
  if (m_params->autostart_log){
    printf("Named log file: %s\n", m_log_files.back().c_str());
    currentLogFile.open(m_log_files.back(), ios::out | ios::app);
    printf("Opened log file\n");
    if (currentLogFile){
      currentLogFile << "v1" << endl;
      currentLogFile << MANGOHUD_VERSION << endl;
      currentLogFile << "---------------------SYSTEM INFO---------------------" << endl;
      currentLogFile << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver," << "cpu_governor," << "Wine/Proton," << "sync," << "renderer," << "cpu_scheduler" << endl;
      currentLogFile << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << "," << cpu_governor << "," << wineVersion << "," << HUDElements.sync << "," << HUDElements.sw_stats->engineName << HUDElements.cpu_sched << endl;
      currentLogFile << "--------------------FRAME METRICS--------------------" << endl;
      currentLogFile << "fps," << "frametime," << "cpu_load," << "gpu_load," << "cpu_temp," << "gpu_temp," << "gpu_core_clock," << "gpu_mem_clock," << "gpu_vram_used," << "gpu_power," << "ram_used," << "current_watt,";
      for (size_t i = 0; i < cpuStats.GetCPUData().size(); i++){
        currentLogFile << "cpu" + to_string(i) + ",";
      }
      currentLogFile << "elapsed" << endl;
    }
    printf("Wrote info to log file\n");
  }
  if((!m_params->output_folder.empty()) && (m_params->log_interval != 0)){
    std::thread(logging, m_params).detach();
  }
}

void Logger::stop_logging() {
  if(!m_logging_on) return;
  m_logging_on = false;
  m_log_end = Clock::now();

  std::thread(calculate_benchmark_data, m_params).detach();

  if(!m_params->output_folder.empty() && !m_params->autostart_log) {
    m_log_files.emplace_back(m_params->output_folder + "/" + get_program_name() + "_" + get_log_suffix());
    std::thread(writeFile, m_log_files.back()).detach();
  }
}

void Logger::try_log() {
  if(!is_active()) return;
  if(!m_values_valid) return;
  auto now = Clock::now();
  auto elapsedLog = now - m_log_start;

  currentLogData.previous = elapsedLog;
  currentLogData.fps = fps;
  currentLogData.frametime = frametime;
#ifdef __gnu_linux__
  currentLogData.current_watt = Battery_Stats.current_watt;
#endif
  m_log_array.push_back(currentLogData);
  if (m_params->autostart_log)
    writeFileContinuous(currentLogFile);
  if(m_params->log_duration && (elapsedLog >= std::chrono::seconds(m_params->log_duration))){
    stop_logging();
  }
}

void Logger::wait_until_data_valid() {
  std::unique_lock<std::mutex> lck(m_values_valid_mtx);
  while(! m_values_valid) m_values_valid_cv.wait(lck);
}

void Logger::notify_data_valid() {
  std::unique_lock<std::mutex> lck(m_values_valid_mtx);
  m_values_valid = true;
  m_values_valid_cv.notify_all();
}

void Logger::upload_last_log() {
  if(m_log_files.empty()) return;
  std::thread(upload_file, m_log_files.back()).detach();
}

void Logger::upload_last_logs() {
  if(m_log_files.empty()) return;
  std::thread(upload_files, m_log_files).detach();
}

void autostart_log(int sleep) {
  os_time_sleep(sleep * 1000000);
  logger->start_logging();
}
