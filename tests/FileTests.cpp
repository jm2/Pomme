#include "Pomme.h"
#include "PommeFiles.h"

#include <fstream>
#include <iostream>
#include <string>

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "           \
                << #condition << '\n';                                          \
      return 1;                                                                 \
    }                                                                           \
  } while (false)

static std::string ReadCStringFile(const fs::path &path) {
  std::string contents;
  std::ifstream file(path, std::ios::binary);
  std::getline(file, contents, '\0');
  return contents;
}

int main() {
  const fs::path testDirectory =
      fs::temp_directory_path() / "pomme-atomic-file-tests";
  fs::remove_all(testDirectory);
  fs::create_directories(testDirectory);

  Pomme::Files::Init();

  short foundVRefNum = -1;
  long foundDirID = -1;
  CHECK(FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder,
                   nullptr, &foundDirID) == paramErr);
  CHECK(FindFolder(1, kPreferencesFolderType, kDontCreateFolder,
                   &foundVRefNum, &foundDirID) == nsvErr);

  const fs::path sourcePath = testDirectory / "source";
  const fs::path destinationPath = testDirectory / "destination";
  FSSpec source = Pomme::Files::HostPathToFSSpec(sourcePath);
  FSSpec destination = Pomme::Files::HostPathToFSSpec(destinationPath);

  CHECK(FSpCreate(&source, 'TEST', 'DATA', 0) == noErr);
  short refNum = -1;
  CHECK(FSpOpenDF(&source, fsRdWrPerm, &refNum) == noErr);
  char replacement[] = "replacement";
  long replacementSize = sizeof(replacement);
  CHECK(FSWrite(refNum, &replacementSize, replacement) == noErr);
  CHECK(replacementSize == static_cast<long>(sizeof(replacement)));
  CHECK(FSClose(refNum) == noErr);

  {
    std::ofstream oldDestination(destinationPath);
    oldDestination << "old";
  }

  CHECK(FSpAtomicReplace(&source, &destination) == noErr);
  CHECK(!fs::exists(sourcePath));
  CHECK(fs::is_regular_file(destinationPath));

  CHECK(ReadCStringFile(destinationPath) == "replacement");

  // Reading through EOF on a read/write fork must not make a successful close
  // look like a write-side I/O failure.
  CHECK(FSpOpenDF(&destination, fsRdWrPerm, &refNum) == noErr);
  char readBuffer[64];
  long invalidReadSize = 1;
  long zeroSize = 0;
  CHECK(FSRead(refNum, nullptr, readBuffer) == paramErr);
  CHECK(FSRead(refNum, &invalidReadSize, nullptr) == paramErr);
  CHECK(FSRead(refNum, &zeroSize, nullptr) == noErr);
  CHECK(FSWrite(refNum, &zeroSize, nullptr) == noErr);
  CHECK(GetEOF(refNum, nullptr) == paramErr);
  long readSize = sizeof(readBuffer);
  CHECK(FSRead(refNum, &readSize, readBuffer) == eofErr);
  CHECK(readSize == static_cast<long>(sizeof(replacement)));
  CHECK(FSClose(refNum) == noErr);

  FSSpec missing = Pomme::Files::HostPathToFSSpec(testDirectory / "missing");
  CHECK(FSpAtomicReplace(&missing, &destination) == fnfErr);
  CHECK(fs::is_regular_file(destinationPath));
  CHECK(ReadCStringFile(destinationPath) == "replacement");

  // The API deliberately rejects cross-directory moves. On Windows,
  // std::filesystem::rename may otherwise fall back to a non-atomic copy/delete
  // when the directories reside on different host volumes.
  const fs::path otherDirectory = testDirectory / "other";
  fs::create_directory(otherDirectory);
  const fs::path crossDirectorySourcePath = testDirectory / "cross-source";
  const fs::path crossDirectoryDestinationPath = otherDirectory / "cross-dest";
  {
    std::ofstream sourceFile(crossDirectorySourcePath);
    sourceFile << "new";
    std::ofstream destinationFile(crossDirectoryDestinationPath);
    destinationFile << "old";
  }
  FSSpec crossDirectorySource =
      Pomme::Files::HostPathToFSSpec(crossDirectorySourcePath);
  FSSpec crossDirectoryDestination =
      Pomme::Files::HostPathToFSSpec(crossDirectoryDestinationPath);
  CHECK(FSpAtomicReplace(&crossDirectorySource, &crossDirectoryDestination) ==
        paramErr);
  CHECK(ReadCStringFile(crossDirectorySourcePath) == "new");
  CHECK(ReadCStringFile(crossDirectoryDestinationPath) == "old");

  FSSpec impossible = Pomme::Files::HostPathToFSSpec(
      testDirectory / "missing-parent" / "file");
  CHECK(FSpCreate(&impossible, 'TEST', 'DATA', 0) == ioErr);
  CHECK(ReadCStringFile(destinationPath) == "replacement");

  fs::remove_all(testDirectory);
  return 0;
}
