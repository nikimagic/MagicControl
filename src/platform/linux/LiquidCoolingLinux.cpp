#include "MagicControl/LiquidCooling.h"
namespace mc {
std::shared_ptr<LiquidCoolingController> createLiquidCoolingController(){return {};}
void suspendLiquidCooling(){}
std::string liquidCoolingDiagnosticsJson(){return "{\"schema\":1,\"backend\":null,\"session_created\":false,\"status\":\"Linux LPP BLE/native policy transport not connected; no discovery or commands performed\"}";}
}
