#include "host_info.hpp"

#include <sstream>
#include "hwinfo/hwinfo.h"
#include <iomanip>

namespace monolith {
namespace hardware {

std::string get_info() {

   std::stringstream ss;

   ss << std::endl << "Hardware Report:" << std::endl << std::endl;
   hwinfo::CPU cpu;
   ss << "----------------------------------- CPU -----------------------------------" << std::endl;
   ss << std::left << std::setw(20) << "vendor:";
   ss << cpu.vendor() << std::endl;
   ss << std::left << std::setw(20) << "model:";
   ss << cpu.modelName() << std::endl;
   ss << std::left << std::setw(20) << "physical cores:";
   ss << cpu.numPhysicalCores() << std::endl;
   ss << std::left << std::setw(20) << "logical cores:";
   ss << cpu.numLogicalCores() << std::endl;
   ss << std::left << std::setw(20) << "max frequency:";
   ss << cpu.maxClockSpeed_kHz() << std::endl;
   ss << std::left << std::setw(20) << "regular frequency:";
   ss << cpu.regularClockSpeed_kHz() << std::endl;
   ss << std::left << std::setw(20) << "current frequency:";
   ss << hwinfo::CPU::currentClockSpeed_kHz() << std::endl;
   ss << std::left << std::setw(20) << "cache size:";
   ss << cpu.cacheSize_Bytes() << std::endl;

   hwinfo::OS os;
   ss << "----------------------------------- OS ------------------------------------" << std::endl;
   ss << std::left << std::setw(20) << "Operating System:";
   ss << os.fullName() <<std::endl;
   ss << std::left << std::setw(20) << "short name:";
   ss << os.name() <<std::endl;
   ss << std::left << std::setw(20) << "version:";
   ss << os.version() <<std::endl;
   ss << std::left << std::setw(20) << "kernel:";
   ss << os.kernel() <<std::endl;
   ss << std::left << std::setw(20) << "architecture:";
   ss << (os.is32bit() ? "32 bit" : "64 bit") << std::endl;
   ss << std::left << std::setw(20) << "endianess:";
   ss << (os.isLittleEndian() ? "little endian" : "big endian") << std::endl;

   hwinfo::GPU gpu;
   ss << "----------------------------------- GPU -----------------------------------" << std::endl;
   ss << std::left << std::setw(20) << "vendor:";
   ss << gpu.vendor() << std::endl;
   ss << std::left << std::setw(20) << "model:";
   ss << gpu.name() << std::endl;
   ss << std::left << std::setw(20) << "driverVersion:";
   ss << gpu.driverVersion() << std::endl;
   ss << std::left << std::setw(20) << "memory [MiB]:";
   ss << static_cast<double>(gpu.memory_Bytes()) / 1024.0 / 1024.0 << std::endl;

   hwinfo::RAM ram;
   ss << "----------------------------------- RAM -----------------------------------" << std::endl;
   ss << std::left << std::setw(20) << "vendor:";
   ss << ram.vendor() << std::endl;
   ss << std::left << std::setw(20) << "model:";
   ss << ram.model() << std::endl;
   ss << std::left << std::setw(20) << "name:";
   ss << ram.name() << std::endl;
   ss << std::left << std::setw(20) << "serial-number:";
   ss << ram.serialNumber() << std::endl;
   ss << std::left << std::setw(20) << "size [MiB]:";
   ss << static_cast<double>(ram.totalSize_Bytes()) / 1024.0 / 1024.0 << std::endl;

   hwinfo::MainBoard main_board;
   ss << "------------------------------- Main Board --------------------------------" << std::endl;
   ss << std::left << std::setw(20) << "vendor:";
   ss << main_board.vendor() << std::endl;
   ss << std::left << std::setw(20) << "name:";
   ss << main_board.name() << std::endl;
   ss << std::left << std::setw(20) << "version:";
   ss << main_board.version() << std::endl;
   ss << std::left << std::setw(20) << "serial-number:";
   ss << ram.serialNumber() << std::endl;

   std::vector<hwinfo::Disk> disks = hwinfo::getAllDisks();
   int counter = 0;
   ss << "--------------------------------- Disks -----------------------------------" << std::endl;
   for (const auto& disk: disks) {
      ss << "Disk " << counter++ << ":" << std::endl;
      ss << std::left << std::setw(20) << "  vendor:";
      ss << disk.vendor() << std::endl;
      ss << std::left << std::setw(20) << "  model:";
      ss << disk.model() << std::endl;
      ss << std::left << std::setw(20) << "  serial-number:";
      ss << disk.serialNumber() << std::endl;
      ss << std::left << std::setw(20) << "  size:";
      ss << disk.size_Bytes() << std::endl;
   }
   ss << "---------------------------------------------------------------------------" << std::endl;

   hwinfo::getAllDisks();

   return ss.str();
}

} // namespace hardware
} // namespace monolith