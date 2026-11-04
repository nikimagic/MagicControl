#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "TestBoardFixture.h"
#include <utility>

// Linked only into magiccontrol_tests. Hardware transports are still fake:
// this fixture supplies deterministic SMBIOS for the existing Wujie contract
// tests, without adding a production override or weakening admission.
namespace mc {
namespace {
BoardDescriptor wujieBoard(){
    BoardDescriptor board;
    board.manufacturer="MECHREVO";
    board.baseBoard="WUJIE Series-X5SP4NAG";
    return board;
}
BoardDescriptor fixture=wujieBoard();
}
namespace tests {
void setTestBoard(BoardDescriptor board){fixture=std::move(board);}
void resetTestBoard(){fixture=wujieBoard();}
}
BoardDescriptor readBoardDescriptor(){
    auto board=fixture;identifyDevice(board);return board;
}
}
