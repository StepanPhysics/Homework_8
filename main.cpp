#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>
#include <thread>
#include <mutex>
#include <future>

#include "CRC32.hpp"
#include "IO.hpp"

void replaceLastFourBytes(std::vector<char> &data, uint32_t value) {
  std::copy_n(reinterpret_cast<const char *>(&value), 4, data.end() - 4);
}

bool attemptHackInRange(
    const std::vector<char>& original, const std::string& injection, uint32_t originalCrc32,
    uint32_t start, uint32_t end, std::vector<char>& result, std::mutex& outputMutex, int threadId) {

  std::vector<char> localResult(original.size() + injection.size() + 4);
  auto it = std::copy(original.begin(), original.end(), localResult.begin());
  std::copy(injection.begin(), injection.end(), it);

  for (uint32_t i = start; i < end; ++i) {
    replaceLastFourBytes(localResult, i);
    auto currentCrc32 = crc32(localResult.data(), localResult.size());
    if (currentCrc32 == originalCrc32) {
      result = std::move(localResult);
      return true;
    }
    // Промежуточный вывод прогресса
    if (i % 1000000 == 0) {
      std::lock_guard<std::mutex> lock(outputMutex);
      std::cout << "Thread " << threadId << ": Progress " 
                << static_cast<double>(i - start) / (end - start) * 100 
                << "%\n";
    }
  }
  return false;
}

std::vector<char> hack(const std::vector<char>& original, const std::string& injection) {
  const uint32_t originalCrc32 = crc32(original.data(), original.size());
  const unsigned int numThreads = std::thread::hardware_concurrency();
  const uint32_t maxVal = std::numeric_limits<uint32_t>::max();

  std::vector<std::future<bool>> futures;
  std::vector<char> result;
  std::mutex resultMutex;
  std::mutex outputMutex; 
  bool found = false;

  uint32_t rangeSize = maxVal / numThreads;
  for (unsigned int t = 0; t < numThreads; ++t) {
    uint32_t start = t * rangeSize;
    uint32_t end = (t == numThreads - 1) ? maxVal : start + rangeSize;

    futures.emplace_back(std::async(std::launch::async, [&, start, end, t]() {
      std::vector<char> localResult;
      if (attemptHackInRange(original, injection, originalCrc32, start, end, localResult, outputMutex, t)) {
        std::lock_guard<std::mutex> lock(resultMutex);
        if (!found) {
          result = std::move(localResult);
          found = true;
        }
        return true;
      }
      return false;
    }));
  }

  for (auto& f : futures) {
    if (f.get() && found) break;
  }

  if (!found) {
    throw std::logic_error("Can't hack");
  }
  std::cout << "Success\n";
  return result;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Call with two args: " << argv[0] << " <input file> <output file>\n";
    return 1;
  }

  try {
    const std::vector<char> data = readFromFile(argv[1]);
    const std::vector<char> badData = hack(data, "He-he-he");
    writeToFile(argv[2], badData);
  } catch (std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 2;
  }
  return 0;
}



