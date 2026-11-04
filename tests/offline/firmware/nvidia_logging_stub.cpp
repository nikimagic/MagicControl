#include <string_view>
// Isolated collector fixture: never initialize the product logger/providers.
namespace mc {void logMessage(std::string_view,std::string_view){}}
