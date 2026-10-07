// A placement in the merged patch has to be turned back into the mod's own
// archive and offset before the mod's copy can be read for a combine. When
// several of a mod's archives share one patch archive, that means picking the
// block the offset falls in.
#include "Engine/Vfs/mod_merge_internal.h"
#include <iostream>

using namespace dingosdk::mods::detail;

namespace {
int failures = 0;
void check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << "\n"; ++failures; }
}
// A mod's archive: the package folder it is in and its number there.
std::pair<std::string, std::uint16_t> of(const std::string& directory, std::uint16_t archive) {
    return {directory, archive};
}
bool lands(const ArchivePlacement& placement, std::string_view directory, std::uint16_t archive, std::uint32_t offset,
           std::uint16_t expected_archive, std::uint32_t expected_offset) {
    return unshift(placement, directory, archive, offset) && archive == expected_archive && offset == expected_offset;
}
} // namespace

int main() {
    const std::string initial = "configurations/layout/initialinstallpackage";
    const std::string standard = "configurations/layout/defaultinstallpackage";

    // The usual launch placement: each archive at the start of an index of its own.
    ArchivePlacement own;
    own.at[of(initial, 1)] = {4, 0};
    own.at[of(standard, 1)] = {9, 0};
    check(lands(own, initial, 4, 1234, 1, 1234), "an archive with an index of its own keeps its offsets");
    check(lands(own, standard, 9, 0, 1, 0), "the other package folder has its own blocks");
    std::uint16_t archive = 9;
    std::uint32_t offset = 10;
    check(!unshift(own, initial, archive, offset) && archive == 9 && offset == 10,
          "an archive the mod has no block in is left alone");

    // A mod added while the game runs: archives 1, 2 and 3 appended to the patch's archive 1.
    ArchivePlacement appended;
    appended.at[of(initial, 1)] = {1, 5000};
    appended.at[of(initial, 2)] = {1, 9096};
    appended.at[of(initial, 3)] = {1, 20000};
    check(lands(appended, initial, 1, 5000, 1, 0), "the first block starts at its own byte 0");
    check(lands(appended, initial, 1, 9095, 1, 4095), "the last byte of the first block is still the first archive");
    check(lands(appended, initial, 1, 9096, 2, 0), "an offset in the second block is the second archive");
    check(lands(appended, initial, 1, 15000, 2, 5904), "offsets are counted from the block they fall in");
    check(lands(appended, initial, 1, 20001, 3, 1), "and so on for a third");
    archive = 1;
    offset = 4999;
    check(!unshift(appended, initial, archive, offset), "bytes before the mod's first block are not the mod's");

    // An empty archive's block starts where the next one does; the data is in the next.
    ArchivePlacement empty_first;
    empty_first.at[of(initial, 1)] = {1, 700};
    empty_first.at[of(initial, 2)] = {1, 700};
    check(lands(empty_first, initial, 1, 700, 2, 0) && lands(empty_first, initial, 1, 900, 2, 200),
          "an empty archive's block yields to the one that follows it");

    if (failures) return 1;
    std::cout << "mod merge unshift: ok\n";
    return 0;
}
