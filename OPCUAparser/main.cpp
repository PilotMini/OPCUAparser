#include <stdio.h>
#include <iostream>
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>

#pragma comment(lib, "open62541.lib")
#pragma comment(lib, "open-object/open62541-object.lib")
#pragma comment(lib, "open-plugins/open62541-plugins.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "IPHLPAPI.lib")

using json = nlohmann::json;

// Функция для преобразования UA_NodeId в строку
std::string nodeIdToString(const UA_NodeId& nodeId) {
    if (nodeId.identifierType == UA_NODEIDTYPE_NUMERIC) {
        return std::to_string(nodeId.namespaceIndex) + ":" + std::to_string(nodeId.identifier.numeric);
    }
    else if (nodeId.identifierType == UA_NODEIDTYPE_STRING) {
        return std::to_string(nodeId.namespaceIndex) + ":" + std::string((char*)nodeId.identifier.string.data, nodeId.identifier.string.length);
    }
    return "unknown";
}

// Функция для получения имени типа данных
std::string getDataTypeName(UA_Client* client, const UA_NodeId& dataTypeId) {
    UA_QualifiedName dataTypeName;
    UA_QualifiedName_init(&dataTypeName);
    UA_StatusCode status = UA_Client_readBrowseNameAttribute(client, dataTypeId, &dataTypeName);
    if (status == UA_STATUSCODE_GOOD) {
        return std::string((char*)dataTypeName.name.data, dataTypeName.name.length);
    }
    return "unknown";
}

// Функция для рекурсивного обхода узлов и записи в файл
void browseNodes(UA_Client* client, UA_NodeId nodeId, std::ofstream& outFile, std::unordered_set<std::string>& visitedNodes, const std::string& indent = "") {
    // Преобразуем NodeId в строку для хранения в множестве
    std::string nodeIdStr = nodeIdToString(nodeId);

    // Если узел уже посещен, пропускаем его
    if (visitedNodes.find(nodeIdStr) != visitedNodes.end()) {
        return;
    }

    // Добавляем узел в множество посещенных
    visitedNodes.insert(nodeIdStr);

    UA_BrowseRequest* bReq = new UA_BrowseRequest;
    UA_BrowseRequest_init(bReq);
    bReq->requestedMaxReferencesPerNode = 0;
    bReq->nodesToBrowse = UA_BrowseDescription_new();
    bReq->nodesToBrowseSize = 1;
    bReq->nodesToBrowse[0].nodeId = nodeId;
    bReq->nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq->nodesToBrowse[0].includeSubtypes = true;
    bReq->nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResponse* bResp = new UA_BrowseResponse;
    *bResp = UA_Client_Service_browse(client, *bReq);
    if (bResp->responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for (size_t i = 0; i < bResp->resultsSize; ++i) {
            for (size_t j = 0; j < bResp->results[i].referencesSize; ++j) {
                UA_ReferenceDescription ref = bResp->results[i].references[j];

                // Игнорируем системные узлы (ns=0)
                if (ref.nodeId.nodeId.namespaceIndex == 0) {
                    continue;
                }

                // Выводим только переменные (UA_NODECLASS_VARIABLE)
                if (ref.nodeClass == UA_NODECLASS_VARIABLE) {
                    std::string displayName = std::string((char*)ref.displayName.text.data, ref.displayName.text.length);

                    // Записываем информацию о узле в файл
                    outFile << indent << "Node: " << displayName
                        << " (NodeId: " << ref.nodeId.nodeId.namespaceIndex << ":"
                        << nodeIdToString(ref.nodeId.nodeId) << ")"
                        << " [Class: " << ref.nodeClass << "]" << std::endl;

                    // Читаем значение переменной
                    UA_Variant value;
                    UA_Variant_init(&value);
                    UA_StatusCode status = UA_Client_readValueAttribute(client, ref.nodeId.nodeId, &value);
                    if (status == UA_STATUSCODE_GOOD) {
                        // Получаем тип данных
                        UA_NodeId dataTypeId;
                        UA_NodeId_init(&dataTypeId);
                        status = UA_Client_readDataTypeAttribute(client, ref.nodeId.nodeId, &dataTypeId);
                        if (status == UA_STATUSCODE_GOOD) {
                            std::string dataTypeName = getDataTypeName(client, dataTypeId);
                            outFile << indent << "  DataType: " << dataTypeName << std::endl;
                        }

                        // Выводим значение переменной
                        if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
                            double tagValue = *(double*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
                            float tagValue = *(float*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
                            int32_t tagValue = *(int32_t*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT16])) {
                            int16_t tagValue = *(int16_t*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
                            bool tagValue = *(bool*)value.data;
                            outFile << indent << "  Value: " << (tagValue ? "true" : "false") << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_STRING])) {
                            UA_String strValue = *(UA_String*)value.data;
                            outFile << indent << "  Value: " << std::string((char*)strValue.data, strValue.length) << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
                            int32_t tagValue = *(uint32_t*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT16])) {
                            uint16_t tagValue = *(uint16_t*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_ENUMERATION])) {
                            uint32_t tagValue = *(uint32_t*)value.data;
                            outFile << indent << "  Value: " << tagValue << std::endl;
                        }
                        else {
                            outFile << indent << "  Value: <unsupported type>" << std::endl;
                        }
                    }
                    UA_Variant_clear(&value);
                }

                // Рекурсивно обходим дочерние узлы
                browseNodes(client, ref.nodeId.nodeId, outFile, visitedNodes, indent + "  ");
            }
        }
    }
    delete bReq;
    delete bResp;
}

int main(int argc, char* argv[]) {
    std::string ip = "";
    if (argc > 1) {
        ip = argv[1];
        std::cout << ip << std::endl;
    }
    else {
        const std::string configPath = "config.json";
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            throw std::runtime_error("Error open config file: " + configPath);
            return -1;
        }

        json config;
        configFile >> config;
        configFile.close();
        std::cout << config.dump() << std::endl;
        // Получаем IP-адрес из JSON
        if (!config.contains("ip")) {
            throw std::runtime_error("Server IP not found in config.");
            return -1;
        }

        ip = config["ip"];
    }

    // Инициализация клиента
    UA_Client* client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    // Подключение к OPC-UA серверу
    UA_StatusCode status = UA_Client_connect(client, ip.c_str());
    if (status != UA_STATUSCODE_GOOD) {
        std::cerr << "Failed to connect to server: " << UA_StatusCode_name(status) << std::endl;
        UA_Client_delete(client);
        return -1;
    }

    // Открываем файл для записи
    std::ofstream outFile("opcua_tags.txt");
    if (!outFile.is_open()) {
        std::cerr << "Failed to open file for writing!" << std::endl;
        UA_Client_delete(client);
        return -1;
    }

    // Множество для отслеживания посещенных узлов
    std::unordered_set<std::string> visitedNodes;

    // Начинаем обход с корневого узла (ObjectsFolder)
    UA_NodeId rootNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    browseNodes(client, rootNodeId, outFile, visitedNodes);

    // Закрываем файл
    outFile.close();

    // Очистка
    UA_Client_delete(client);

    std::cout << "All tags in opcua_tags.txt" << std::endl;
    return 0;
}