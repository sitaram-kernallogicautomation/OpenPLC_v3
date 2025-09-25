//-----------------------------------------------------------------------------
// Copyright 2024 OpenPLC Contributors
// This file is part of the OpenPLC Software Stack.
//
// OpenPLC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OpenPLC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenPLC.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// This file implements OPC UA server functionality for OpenPLC.
// It exposes all PLC runtime variables as OPC UA nodes for reading and writing.
// 
// Features:
// - Scans all OpenPLC runtime variables (bool_input, bool_output, int_input, etc.)
// - Creates corresponding OPC UA nodes in the address space
// - Handles read/write operations from OPC UA clients
// - Thread-safe access to PLC variables using mutex locks
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

// OPC UA includes
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/accesscontrol_default.h>

#include "ladder.h"

// Global variables
static UA_Server *g_opcua_server = NULL;
static pthread_mutex_t opcua_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_opcua_running = false;
static UA_UInt16 g_namespace_index = 1;

// Binding passed as node context for callbacks
typedef struct OpcVarBinding {
    void *variablePtr; // original PLC pointer (unused for reads now)
    const UA_DataType *dataType;
    // Shadow value pointer owned by OPC UA; read callback uses only this
    void *shadowPtr;
    struct OpcVarBinding *next;
} OpcVarBinding;

static const char* uaTypeName(const UA_DataType *t) {
    if (!t) return "<null>";
    for (size_t i = 0; i < UA_TYPES_COUNT; i++) {
        if (&UA_TYPES[i] == t) return UA_TYPES[i].typeName;
    }
    return "<unknown>";
}

static void formatNodeId(const UA_NodeId *id, char *buf, size_t buflen) {
    if (!id) { snprintf(buf, buflen, "(null)"); return; }
    switch (id->identifierType) {
        case UA_NODEIDTYPE_NUMERIC:
            snprintf(buf, buflen, "ns=%u;i=%u", id->namespaceIndex, id->identifier.numeric);
            break;
        case UA_NODEIDTYPE_STRING:
            snprintf(buf, buflen, "ns=%u;s=%.*s", id->namespaceIndex, (int)id->identifier.string.length, id->identifier.string.data ? (const char*)id->identifier.string.data : "");
            break;
        default:
            snprintf(buf, buflen, "ns=%u;?(type=%u)", id->namespaceIndex, (unsigned)id->identifierType);
    }
}

// Head of linked list of all bindings for shadow synchronization
static OpcVarBinding *g_binding_head = NULL;

// Simple node tracking for periodic updates
struct OpcNodeInfo {
    UA_NodeId nodeId;
    void *variablePtr;
    const UA_DataType *dataType;
    OpcNodeInfo *next;
};
static OpcNodeInfo *g_node_list = NULL;
static int g_node_count = 0;

// onWrite callback for simple variable nodes: copy client value into PLC memory
static void onVariableValueWrite(UA_Server *server,
                                const UA_NodeId *sessionId,
                                void *sessionContext,
                                const UA_NodeId *nodeId,
                                void *nodeContext,
                                const UA_NumericRange *range,
                                const UA_DataValue *data) {
    (void)server;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)range;
    if (!nodeContext || !data || !data->hasValue) return;
    OpcNodeInfo *info = (OpcNodeInfo*)nodeContext;
    if (!info || !info->variablePtr || !info->dataType) return;
    if (!UA_Variant_isScalar(&data->value) || data->value.data == NULL || data->value.type == NULL) return;
    if (data->value.type != info->dataType) return;

    pthread_mutex_lock(&bufferLock);
    if (info->dataType == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        *(IEC_BOOL*)info->variablePtr = *(const UA_Boolean*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_BYTE]) {
        *(IEC_BYTE*)info->variablePtr = *(const UA_Byte*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_SBYTE]) {
        *(IEC_SINT*)info->variablePtr = *(const UA_SByte*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_INT16]) {
        *(IEC_INT*)info->variablePtr = *(const UA_Int16*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_INT32]) {
        *(IEC_DINT*)info->variablePtr = *(const UA_Int32*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_INT64]) {
        *(IEC_LINT*)info->variablePtr = *(const UA_Int64*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_UINT16]) {
        *(IEC_UINT*)info->variablePtr = *(const UA_UInt16*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_UINT32]) {
        *(IEC_UDINT*)info->variablePtr = *(const UA_UInt32*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_UINT64]) {
        *(IEC_ULINT*)info->variablePtr = *(const UA_UInt64*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_FLOAT]) {
        *(IEC_REAL*)info->variablePtr = *(const UA_Float*)data->value.data;
    } else if (info->dataType == &UA_TYPES[UA_TYPES_DOUBLE]) {
        *(IEC_LREAL*)info->variablePtr = *(const UA_Double*)data->value.data;
    }
    pthread_mutex_unlock(&bufferLock);
}

// Node ID definitions for different variable types
#define NAMESPACE_INDEX 1
#define BOOL_INPUT_NS 100
#define BOOL_OUTPUT_NS 101
#define INT_INPUT_NS 102
#define INT_OUTPUT_NS 103
#define DINT_INPUT_NS 104
#define DINT_OUTPUT_NS 105
#define LINT_INPUT_NS 106
#define LINT_OUTPUT_NS 107
#define BYTE_INPUT_NS 108
#define BYTE_OUTPUT_NS 109
#define INT_MEMORY_NS 110
#define DINT_MEMORY_NS 111
#define LINT_MEMORY_NS 112

// Forward declarations
static UA_StatusCode readVariableValue(UA_Server *server, const UA_NodeId *sessionId,
                                     void *sessionContext, const UA_NodeId *nodeId,
                                     void *nodeContext, UA_Boolean sourceTimeStamp,
                                     const UA_NumericRange *range, UA_DataValue *dataValue);

static UA_StatusCode writeVariableValue(UA_Server *server, const UA_NodeId *sessionId,
                                      void *sessionContext, const UA_NodeId *nodeId,
                                      void *nodeContext, const UA_NumericRange *range,
                                      const UA_DataValue *dataValue);

static void addVariableNode(UA_Server *server, const char *nodeName, UA_NodeId parentNodeId,
                           UA_NodeId nodeId, void *variablePtr, UA_DataType *dataType);

static void scanAndCreateNodes(UA_Server *server);
static int createNodesFromLocatedVariables(UA_Server *server);
static void createProgramVariablesFolder(UA_Server *server, UA_NodeId *outFolderId);
static bool resolvePointerFromLocation(const char *location, void **outPtr, const UA_DataType **outType);
static void createFolderStructure(UA_Server *server);
static void registerNamespace(UA_Server *server);

static void logOpen62541AbiInfo(UA_Server *server) {
    char msg[256];
#ifdef UA_OPEN62541_VERSION
    snprintf(msg, sizeof(msg), "open62541 compile-time version: %s\n", UA_OPEN62541_VERSION);
    openplc_log(msg);
#endif
    snprintf(msg, sizeof(msg), "sizeof(UA_DataValue)=%lu sizeof(UA_Variant)=%lu sizeof(UA_ValueCallback)=%lu UA_TYPES_COUNT=%u\n",
             (unsigned long)sizeof(UA_DataValue), (unsigned long)sizeof(UA_Variant), (unsigned long)sizeof(UA_ValueCallback), (unsigned)UA_TYPES_COUNT);
    openplc_log(msg);
    if (!server) return;
    const UA_ServerConfig *cfg = UA_Server_getConfig(server);
    if (!cfg) return;
    const UA_String *sv = &cfg->buildInfo.softwareVersion;
    size_t n = (sv && sv->data && sv->length < sizeof(msg)-32) ? sv->length : 0;
    if (n > 0) {
        memcpy(msg, sv->data, n);
        msg[n] = '\0';
        char out[300];
        snprintf(out, sizeof(out), "open62541 runtime softwareVersion: %s\n", msg);
        openplc_log(out);
    }
}

//-----------------------------------------------------------------------------
// Read handler for OPC UA variables
//-----------------------------------------------------------------------------
static UA_StatusCode readVariableValue(UA_Server *server, const UA_NodeId *sessionId,
                                     void *sessionContext, const UA_NodeId *nodeId,
                                     void *nodeContext, UA_Boolean sourceTimeStamp,
                                     const UA_NumericRange *range, UA_DataValue *dataValue) {
    (void)sessionId;
    (void)sessionContext;
    (void)sourceTimeStamp;
    (void)range;

    if (!nodeContext || !dataValue) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }
    // Do not call UA_DataValue_init here to avoid ABI-size mismatches

    // No locking in the minimal test path
    
    // Cast the node context to binding
    OpcVarBinding *binding = (OpcVarBinding*)nodeContext;
    // No logging in the minimal test path

    if (!binding || !binding->dataType || !binding->shadowPtr) {
        // During node creation, nodeContext may be NULL. Return GOOD with no value.
        dataValue->hasValue = false;
        dataValue->hasStatus = true;
        dataValue->status = UA_STATUSCODE_GOOD;
        dataValue->hasSourceTimestamp = false;
        return UA_STATUSCODE_GOOD;
    }
    // Read solely from shadow cache
    UA_StatusCode sc = UA_STATUSCODE_BADTYPEMISMATCH;
    if (binding->dataType == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        UA_Boolean v = *(const UA_Boolean*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_BYTE]) {
        UA_Byte v = *(const UA_Byte*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_BYTE]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_SBYTE]) {
        UA_SByte v = *(const UA_SByte*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_SBYTE]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_INT16]) {
        UA_Int16 v = *(const UA_Int16*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_INT16]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_INT32]) {
        UA_Int32 v = *(const UA_Int32*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_INT32]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_INT64]) {
        UA_Int64 v = *(const UA_Int64*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_INT64]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_UINT16]) {
        UA_UInt16 v = *(const UA_UInt16*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_UINT16]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_UINT32]) {
        UA_UInt32 v = *(const UA_UInt32*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_UINT32]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_UINT64]) {
        UA_UInt64 v = *(const UA_UInt64*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_UINT64]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_FLOAT]) {
        UA_Float v = *(const UA_Float*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_FLOAT]);
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_DOUBLE]) {
        UA_Double v = *(const UA_Double*)binding->shadowPtr;
        sc = UA_Variant_setScalarCopy(&dataValue->value, &v, &UA_TYPES[UA_TYPES_DOUBLE]);
    }
    //dataValue->hasValue = (sc == UA_STATUSCODE_GOOD);
    dataValue->hasValue = false;
    //dataValue->status = sc;
    dataValue->status = UA_STATUSCODE_GOOD;
    dataValue->hasStatus = true;
    dataValue->hasSourceTimestamp = false;
    //return sc;
    return UA_STATUSCODE_GOOD;
}

//-----------------------------------------------------------------------------
// Write handler for OPC UA variables
//-----------------------------------------------------------------------------
static UA_StatusCode writeVariableValue(UA_Server *server, const UA_NodeId *sessionId,
                                      void *sessionContext, const UA_NodeId *nodeId,
                                      void *nodeContext, const UA_NumericRange *range,
                                      const UA_DataValue *dataValue) {
    (void)server;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)range;

    if (!nodeContext || !dataValue || !dataValue->hasValue) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    // Lock the buffer mutex to ensure thread safety
    pthread_mutex_lock(&bufferLock);
    
    // Cast the node context to binding
    OpcVarBinding *binding = (OpcVarBinding*)nodeContext;
    char nid[64];
    formatNodeId(nodeId, nid, sizeof(nid));
    char log_msg_cb[256];
    snprintf(log_msg_cb, sizeof(log_msg_cb), "OPCUA WRITE cb for %s ctx=%p var=%p type=%s hasValue=%d\n", nid, (void*)binding, binding ? binding->variablePtr : NULL, binding ? uaTypeName(binding->dataType) : "<null>", dataValue->hasValue);
    openplc_log(log_msg_cb);
    
    // Validate variant
    if (!UA_Variant_isScalar(&dataValue->value) || dataValue->value.data == NULL || dataValue->value.type == NULL) {
        pthread_mutex_unlock(&bufferLock);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    if (dataValue->value.type != binding->dataType) {
        pthread_mutex_unlock(&bufferLock);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    // Write the value based on data type into PLC variable and update shadow
    if (binding->dataType == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        UA_Boolean v = *(const UA_Boolean*)dataValue->value.data;
        *(UA_Boolean*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Boolean*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_BYTE]) {
        UA_Byte v = *(const UA_Byte*)dataValue->value.data;
        *(UA_Byte*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Byte*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_SBYTE]) {
        UA_SByte v = *(const UA_SByte*)dataValue->value.data;
        *(UA_SByte*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_SByte*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_INT16]) {
        UA_Int16 v = *(const UA_Int16*)dataValue->value.data;
        *(UA_Int16*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Int16*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_INT32]) {
        UA_Int32 v = *(const UA_Int32*)dataValue->value.data;
        *(UA_Int32*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Int32*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_INT64]) {
        UA_Int64 v = *(const UA_Int64*)dataValue->value.data;
        *(UA_Int64*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Int64*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_UINT16]) {
        UA_UInt16 v = *(const UA_UInt16*)dataValue->value.data;
        *(UA_UInt16*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_UInt16*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_UINT32]) {
        UA_UInt32 v = *(const UA_UInt32*)dataValue->value.data;
        *(UA_UInt32*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_UInt32*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_UINT64]) {
        UA_UInt64 v = *(const UA_UInt64*)dataValue->value.data;
        *(UA_UInt64*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_UInt64*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_FLOAT]) {
        UA_Float v = *(const UA_Float*)dataValue->value.data;
        *(UA_Float*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Float*)binding->shadowPtr = v;
    } else if (binding->dataType == &UA_TYPES[UA_TYPES_DOUBLE]) {
        UA_Double v = *(const UA_Double*)dataValue->value.data;
        *(UA_Double*)binding->variablePtr = v;
        if (binding->shadowPtr) *(UA_Double*)binding->shadowPtr = v;
    } else {
        pthread_mutex_unlock(&bufferLock);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    
    pthread_mutex_unlock(&bufferLock);
    
    return UA_STATUSCODE_GOOD;
}

//-----------------------------------------------------------------------------
// Add a variable node to the OPC UA address space
//-----------------------------------------------------------------------------
static void addVariableNode(UA_Server *server, const char *nodeName, UA_NodeId parentNodeId,
                           UA_NodeId nodeId, void *variablePtr, UA_DataType *dataType) {
    if (!variablePtr) return; // Skip NULL pointers
    if (!dataType) return; // Skip NULL data types
    
    char log_msg[1000];
    char nid[64];
    formatNodeId(&nodeId, nid, sizeof(nid));
    sprintf(log_msg, "Creating node: %s id=%s parent=ns=%u;i=%u var=%p type=%s\n", nodeName, nid, parentNodeId.namespaceIndex, parentNodeId.identifier.numeric, variablePtr, uaTypeName(dataType));
    openplc_log(log_msg);
    
    UA_VariableAttributes attr; 
    UA_VariableAttributes_init(&attr);
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", nodeName);
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    attr.dataType = dataType->typeId;
    attr.valueRank = UA_VALUERANK_SCALAR; // Set as scalar value
    
    // Provide an initial value matching the declared dataType to satisfy type checking
    if (dataType == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        UA_Boolean v = (UA_Boolean)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
    } else if (dataType == &UA_TYPES[UA_TYPES_BYTE]) {
        UA_Byte v = (UA_Byte)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_BYTE]);
    } else if (dataType == &UA_TYPES[UA_TYPES_SBYTE]) {
        UA_SByte v = (UA_SByte)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_SBYTE]);
    } else if (dataType == &UA_TYPES[UA_TYPES_INT16]) {
        UA_Int16 v = (UA_Int16)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_INT16]);
    } else if (dataType == &UA_TYPES[UA_TYPES_INT32]) {
        UA_Int32 v = (UA_Int32)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_INT32]);
    } else if (dataType == &UA_TYPES[UA_TYPES_INT64]) {
        UA_Int64 v = (UA_Int64)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_INT64]);
    } else if (dataType == &UA_TYPES[UA_TYPES_UINT16]) {
        UA_UInt16 v = (UA_UInt16)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_UINT16]);
    } else if (dataType == &UA_TYPES[UA_TYPES_UINT32]) {
        UA_UInt32 v = (UA_UInt32)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_UINT32]);
    } else if (dataType == &UA_TYPES[UA_TYPES_UINT64]) {
        UA_UInt64 v = (UA_UInt64)0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_UINT64]);
    } else if (dataType == &UA_TYPES[UA_TYPES_FLOAT]) {
        UA_Float v = (UA_Float)0.0f;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_FLOAT]);
    } else if (dataType == &UA_TYPES[UA_TYPES_DOUBLE]) {
        UA_Double v = (UA_Double)0.0;
        UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_DOUBLE]);
    }

    // Create simple variable node (no callbacks for now)
    UA_StatusCode retval = UA_Server_addVariableNode(server, nodeId, parentNodeId,
                                                    UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                                    UA_QUALIFIEDNAME(g_namespace_index, nodeName),
                                                    UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                                                    attr, NULL, NULL);
    
    if (retval == UA_STATUSCODE_GOOD) {
        sprintf(log_msg, "Node %s added successfully\n", nodeName);
        openplc_log(log_msg);
        
        // Store node info for periodic updates
        OpcNodeInfo *nodeInfo = (OpcNodeInfo*)malloc(sizeof(OpcNodeInfo));
        if (nodeInfo) {
            nodeInfo->nodeId = nodeId;
            nodeInfo->variablePtr = variablePtr;
            nodeInfo->dataType = dataType;
            pthread_mutex_lock(&opcua_mutex);
            nodeInfo->next = g_node_list;
            g_node_list = nodeInfo;
            g_node_count++;
            pthread_mutex_unlock(&opcua_mutex);
            char reg_msg[256];
            snprintf(reg_msg, sizeof(reg_msg), "Registered node for updates: %s ptr=%p type=%s (total=%d)\n",
                     nodeName, variablePtr, uaTypeName(dataType), g_node_count);
            openplc_log(reg_msg);

            // Attach nodeContext and onWrite callback to support client writes
            UA_Server_setNodeContext(server, nodeId, nodeInfo);
            UA_ValueCallback cb; memset(&cb, 0, sizeof(cb));
            cb.onRead = NULL; // reads are handled by periodic updates
            cb.onWrite = onVariableValueWrite;
            UA_Server_setVariableNode_valueCallback(server, nodeId, cb);
        } else {
            openplc_log("Failed to allocate OpcNodeInfo; node will not be updated periodically\n");
        }
    } else if (retval == UA_STATUSCODE_BADNODEIDEXISTS) {
        sprintf(log_msg, "Node %s already exists, skipping\n", nodeName);
        openplc_log(log_msg);
    } else {
        sprintf(log_msg, "Failed to add node %s: %s\n", nodeName, UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    UA_VariableAttributes_clear(&attr);


}

// Update all OPC UA node values from PLC variables
extern "C" void opcuaUpdateNodeValues() {
    // Debug: always log that this function is called
    //openplc_log("opcuaUpdateNodeValues() called\n");
    
    if (!g_opcua_server || !g_opcua_running) {
        char debug_msg[200];
        sprintf(debug_msg, "opcuaUpdateNodeValues() - server not running (server=%p, running=%d), skipping\n", 
                (void*)g_opcua_server, g_opcua_running);
        openplc_log(debug_msg);
        return;
    }
    
    pthread_mutex_lock(&opcua_mutex);
    OpcNodeInfo *it = g_node_list;
    
    // Debug: log how many nodes we're updating
    int nodeCount = 0;
    while (it != NULL) {
        nodeCount++;
        it = it->next;
    }
    
    // Always log the count, even if 0
    char debug_msg[200];
    //sprintf(debug_msg, "Updating %d OPC UA nodes (server=%p, running=%d)\n", nodeCount, (void*)g_opcua_server, g_opcua_running);
    openplc_log(debug_msg);
    
    it = g_node_list;
    while (it != NULL) {
        if (it->variablePtr && it->dataType) {
            UA_Variant value;
            UA_Variant_init(&value);
            
            // Read current value from PLC variable
            if (it->dataType == &UA_TYPES[UA_TYPES_BOOLEAN]) {
                UA_Boolean v = *(IEC_BOOL*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_BYTE]) {
                UA_Byte v = *(IEC_BYTE*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_BYTE]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_SBYTE]) {
                UA_SByte v = *(IEC_SINT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_SBYTE]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_INT16]) {
                UA_Int16 v = *(IEC_INT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_INT16]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_INT32]) {
                UA_Int32 v = *(IEC_DINT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_INT32]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_INT64]) {
                UA_Int64 v = *(IEC_LINT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_INT64]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_UINT16]) {
                UA_UInt16 v = *(IEC_UINT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_UINT16]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_UINT32]) {
                UA_UInt32 v = *(IEC_UDINT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_UINT32]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_UINT64]) {
                UA_UInt64 v = *(IEC_ULINT*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_UINT64]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_FLOAT]) {
                UA_Float v = *(IEC_REAL*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_FLOAT]);
            } else if (it->dataType == &UA_TYPES[UA_TYPES_DOUBLE]) {
                UA_Double v = *(IEC_LREAL*)it->variablePtr;
                UA_Variant_setScalarCopy(&value, &v, &UA_TYPES[UA_TYPES_DOUBLE]);
            }
            
            // Write value to OPC UA node
            UA_StatusCode retval = UA_Server_writeValue(g_opcua_server, it->nodeId, value);
            if (retval != UA_STATUSCODE_GOOD) {
                // Log error but continue with other nodes
                char log_msg[200];
                sprintf(log_msg, "Failed to update node value: %s\n", UA_StatusCode_name(retval));
                openplc_log(log_msg);
            }
            
            UA_Variant_clear(&value);
        }
        it = it->next;
    }
    pthread_mutex_unlock(&opcua_mutex);
}

//-----------------------------------------------------------------------------
// Register OpenPLC namespace
//-----------------------------------------------------------------------------
static void registerNamespace(UA_Server *server) {
    char log_msg[1000];
    
    // Add OpenPLC namespace
    g_namespace_index = UA_Server_addNamespace(server, "http://openplc.org/");
    if (g_namespace_index == 0) {
        sprintf(log_msg, "Failed to add OpenPLC namespace\n");
        openplc_log(log_msg);
    } else {
        sprintf(log_msg, "OpenPLC namespace registered with index %d\n", g_namespace_index);
        openplc_log(log_msg);
    }
}

//-----------------------------------------------------------------------------
// Create folder structure for organizing variables
//-----------------------------------------------------------------------------
static void createFolderStructure(UA_Server *server) {
    char log_msg[1000];
    sprintf(log_msg, "Creating folder structure...\n");
    openplc_log(log_msg);
    
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    
    // Create main OpenPLC folder
    UA_NodeId openplcFolder = UA_NODEID_NUMERIC(g_namespace_index, 1000);
    UA_ObjectAttributes openplcAttr = UA_ObjectAttributes_default;
    openplcAttr.displayName = UA_LOCALIZEDTEXT("en-US", "OpenPLC");
    openplcAttr.description = UA_LOCALIZEDTEXT("en-US", "OpenPLC Runtime Variables");
    
    UA_StatusCode retval = UA_Server_addObjectNode(server, openplcFolder, objectsFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "OpenPLC"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           openplcAttr, NULL, NULL);
    
    if (retval != UA_STATUSCODE_GOOD && retval != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[1000];
        sprintf(log_msg, "Failed to create OpenPLC folder: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    // Create subfolders for different variable types
    UA_NodeId boolInputFolder = UA_NODEID_NUMERIC(g_namespace_index, 2000);
    UA_ObjectAttributes boolInputAttr = UA_ObjectAttributes_default;
    boolInputAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Boolean Inputs");
    retval = UA_Server_addObjectNode(server, boolInputFolder, openplcFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "BooleanInputs"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           boolInputAttr, NULL, NULL);
    if (retval != UA_STATUSCODE_GOOD && retval != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[1000];
        sprintf(log_msg, "Failed to create BooleanInputs folder: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    UA_NodeId boolOutputFolder = UA_NODEID_NUMERIC(g_namespace_index, 2001);
    UA_ObjectAttributes boolOutputAttr = UA_ObjectAttributes_default;
    boolOutputAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Boolean Outputs");
    retval = UA_Server_addObjectNode(server, boolOutputFolder, openplcFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "BooleanOutputs"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           boolOutputAttr, NULL, NULL);
    if (retval != UA_STATUSCODE_GOOD && retval != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[1000];
        sprintf(log_msg, "Failed to create BooleanOutputs folder: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    UA_NodeId intInputFolder = UA_NODEID_NUMERIC(g_namespace_index, 2002);
    UA_ObjectAttributes intInputAttr = UA_ObjectAttributes_default;
    intInputAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Integer Inputs");
    retval = UA_Server_addObjectNode(server, intInputFolder, openplcFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "IntegerInputs"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           intInputAttr, NULL, NULL);
    if (retval != UA_STATUSCODE_GOOD && retval != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[1000];
        sprintf(log_msg, "Failed to create IntegerInputs folder: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    UA_NodeId intOutputFolder = UA_NODEID_NUMERIC(g_namespace_index, 2003);
    UA_ObjectAttributes intOutputAttr = UA_ObjectAttributes_default;
    intOutputAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Integer Outputs");
    retval = UA_Server_addObjectNode(server, intOutputFolder, openplcFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "IntegerOutputs"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           intOutputAttr, NULL, NULL);
    if (retval != UA_STATUSCODE_GOOD && retval != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[1000];
        sprintf(log_msg, "Failed to create IntegerOutputs folder: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    UA_NodeId memoryFolder = UA_NODEID_NUMERIC(g_namespace_index, 2004);
    UA_ObjectAttributes memoryAttr = UA_ObjectAttributes_default;
    memoryAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Memory Variables");
    retval = UA_Server_addObjectNode(server, memoryFolder, openplcFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "MemoryVariables"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           memoryAttr, NULL, NULL);
    if (retval != UA_STATUSCODE_GOOD && retval != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[1000];
        sprintf(log_msg, "Failed to create MemoryVariables folder: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
    }
    
    sprintf(log_msg, "Folder structure created successfully\n");
    openplc_log(log_msg);
}

//-----------------------------------------------------------------------------
// Scan all PLC variables and create corresponding OPC UA nodes
//-----------------------------------------------------------------------------
static void scanAndCreateNodes(UA_Server *server) {
    char log_msg[1000];
    sprintf(log_msg, "Starting to scan and create OPC UA nodes...\n");
    openplc_log(log_msg);

    // Create base folder structure once
    createFolderStructure(server);

    int added = createNodesFromLocatedVariables(server);

    sprintf(log_msg, "Finished creating OPC UA nodes (%d added)\n", added);
    openplc_log(log_msg);
}

// Create a dedicated folder for program variables
static void createProgramVariablesFolder(UA_Server *server, UA_NodeId *outFolderId) {
    UA_NodeId openplcFolder = UA_NODEID_NUMERIC(g_namespace_index, 1000);
    UA_NodeId programVarsFolder = UA_NODEID_NUMERIC(g_namespace_index, 2100);
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "Program Variables");
    UA_StatusCode rc = UA_Server_addObjectNode(server, programVarsFolder, openplcFolder,
                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                           UA_QUALIFIEDNAME(g_namespace_index, "ProgramVariables"),
                           UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                           attr, NULL, NULL);
    if (rc != UA_STATUSCODE_GOOD && rc != UA_STATUSCODE_BADNODEIDEXISTS) {
        char log_msg[256];
        sprintf(log_msg, "Failed to create ProgramVariables folder: %s\n", UA_StatusCode_name(rc));
        openplc_log(log_msg);
    }
    if (outFolderId) *outFolderId = programVarsFolder;
}


// Resolve pointer and UA type from IEC location token like %IX0.0, %QW10, %MD954
static bool resolvePointerFromLocation(const char *location, void **outPtr, const UA_DataType **outType) {
    if (!location || !outPtr || !outType) return false;
    // Expect a leading '%'
    if (location[0] != '%') return false;
    char area = location[1]; // I,Q,M
    char type = location[2]; // X,B,W,D,L,R,F (F=LREAL for double precision)
    const char *rest = location + 3;

    int index1 = 0;
    int index2 = -1;
    // For bit addresses %IXa.b -> parse a and b
    if (type == 'X') {
        // format: number '.' number
        index1 = atoi(rest);
        const char *dot = strchr(rest, '.');
        if (!dot) return false;
        index2 = atoi(dot + 1);
        if (index2 < 0 || index2 >= 8) return false;
    } else {
        index1 = atoi(rest);
    }

    // Use a more robust approach: try to access the array and handle errors gracefully
    // This approach works for any PLC program regardless of which data types are defined
    
    switch (area) {
        case 'I':
            if (type == 'X') { 
                if (index1>=0 && index1<BUFFER_SIZE && bool_input[index1][index2] != NULL) { 
                    *outPtr = (void*)bool_input[index1][index2]; 
                    *outType = &UA_TYPES[UA_TYPES_BOOLEAN]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'B') { 
                if (index1>=0 && index1<BUFFER_SIZE && byte_input[index1] != NULL) { 
                    *outPtr = (void*)byte_input[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_BYTE]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'W') { 
                if (index1>=0 && index1<BUFFER_SIZE && int_input[index1] != NULL) { 
                    *outPtr = (void*)int_input[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT16]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'D') { 
                if (index1>=0 && index1<BUFFER_SIZE && dint_input[index1] != NULL) { 
                    *outPtr = (void*)dint_input[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT32]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'L') { 
                if (index1>=0 && index1<BUFFER_SIZE && lint_input[index1] != NULL) { 
                    *outPtr = (void*)lint_input[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT64]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'R') { 
                if (index1>=0 && index1<BUFFER_SIZE && real_input[index1] != NULL) { 
                    *outPtr = (void*)real_input[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_FLOAT]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'F') { 
                if (index1>=0 && index1<BUFFER_SIZE && lreal_input[index1] != NULL) { 
                    *outPtr = (void*)lreal_input[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_DOUBLE]; 
                    return *outPtr != NULL; 
                } 
            }
            break;
        case 'Q':
            if (type == 'X') { 
                if (index1>=0 && index1<BUFFER_SIZE && bool_output[index1][index2] != NULL) { 
                    *outPtr = (void*)bool_output[index1][index2]; 
                    *outType = &UA_TYPES[UA_TYPES_BOOLEAN]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'B') { 
                if (index1>=0 && index1<BUFFER_SIZE && byte_output[index1] != NULL) { 
                    *outPtr = (void*)byte_output[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_BYTE]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'W') { 
                if (index1>=0 && index1<BUFFER_SIZE && int_output[index1] != NULL) { 
                    *outPtr = (void*)int_output[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT16]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'D') { 
                if (index1>=0 && index1<BUFFER_SIZE && dint_output[index1] != NULL) { 
                    *outPtr = (void*)dint_output[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT32]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'L') { 
                if (index1>=0 && index1<BUFFER_SIZE && lint_output[index1] != NULL) { 
                    *outPtr = (void*)lint_output[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT64]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'R') { 
                if (index1>=0 && index1<BUFFER_SIZE && real_output[index1] != NULL) { 
                    *outPtr = (void*)real_output[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_FLOAT]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'F') { 
                if (index1>=0 && index1<BUFFER_SIZE && lreal_output[index1] != NULL) { 
                    *outPtr = (void*)lreal_output[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_DOUBLE]; 
                    return *outPtr != NULL; 
                } 
            }
            break;
        case 'M':
            if (type == 'W') { 
                if (index1>=0 && index1<BUFFER_SIZE && int_memory[index1] != NULL) { 
                    *outPtr = (void*)int_memory[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT16]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'D') { 
                if (index1>=0 && index1<BUFFER_SIZE && dint_memory[index1] != NULL) { 
                    *outPtr = (void*)dint_memory[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT32]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'L') { 
                if (index1>=0 && index1<BUFFER_SIZE && lint_memory[index1] != NULL) { 
                    *outPtr = (void*)lint_memory[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_UINT64]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'R') { 
                if (index1>=0 && index1<BUFFER_SIZE && real_memory[index1] != NULL) { 
                    *outPtr = (void*)real_memory[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_FLOAT]; 
                    return *outPtr != NULL; 
                } 
            }
            if (type == 'F') { 
                if (index1>=0 && index1<BUFFER_SIZE && lreal_memory[index1] != NULL) { 
                    *outPtr = (void*)lreal_memory[index1]; 
                    *outType = &UA_TYPES[UA_TYPES_DOUBLE]; 
                    return *outPtr != NULL; 
                } 
            }
            break;
    }
    return false;
}

// Parse VARIABLES.csv and create one node per PLC program variable

// Fallback: parse LOCATED_VARIABLES.h entries like __LOCATED_VAR(BOOL,__QX0_1,Q,X,0,1)
static int createNodesFromLocatedVariables(UA_Server *server) {
    UA_NodeId programFolder;
    createProgramVariablesFolder(server, &programFolder);

    // Try multiple common locations for LOCATED_VARIABLES.h
    const char *hdrCandidates[] = {
        "LOCATED_VARIABLES.h",
        "./LOCATED_VARIABLES.h",
        "core/LOCATED_VARIABLES.h",
        "./core/LOCATED_VARIABLES.h",
        "../core/LOCATED_VARIABLES.h",
        "../LOCATED_VARIABLES.h"
    };
    FILE *f = NULL;
    for (size_t i=0; i<sizeof(hdrCandidates)/sizeof(hdrCandidates[0]); i++) {
        f = fopen(hdrCandidates[i], "r");
        if (f) break;
    }
    if (!f) {
        char log_msg[256];
        sprintf(log_msg, "LOCATED_VARIABLES.h not found in common locations. No nodes created.\n");
        openplc_log(log_msg);
        return 0;
    }
    int added = 0;
    int seen = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Trim leading spaces
        char *p = line;
        while (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') p++;
        // Quick filter: look for macro substring
        if (strstr(p, "__LOCATED_VAR(") == NULL) continue;
        seen++;
        // Extract inside parentheses
        char *lpar = strchr(p, '(');
        char *rpar = lpar ? strrchr(lpar, ')') : NULL;
        if (!lpar || !rpar) continue;
        *rpar = '\0';
        char *args = lpar + 1;

        // Tokenize by comma
        char *tokens[8]; int n=0; char *saveptr=NULL; char *tok = strtok_r(args, ",", &saveptr);
        while (tok && n<8) { tokens[n++] = tok; tok = strtok_r(NULL, ",", &saveptr); }
        if (n < 6) continue;

        // tokens: [0]=IEC type, [1]=__NAME, [2]=Area(I/Q/M), [3]=Type(X/B/W/D/L), [4]=idx1, [5]=idx2
        char *nameTok = tokens[1];
        while (*nameTok==' '||*nameTok=='\t') nameTok++;
        if (strncmp(nameTok, "__", 2) == 0) nameTok += 2; // strip leading __
        // Make a copy for display name
        char dispName[128];
        snprintf(dispName, sizeof(dispName), "%s", nameTok);

        // Compose a location string to reuse resolver
        while (tokens[2][0]==' '||tokens[2][0]=='\t') tokens[2]++;
        while (tokens[3][0]==' '||tokens[3][0]=='\t') tokens[3]++;
        char area = tokens[2][0];
        char typ = tokens[3][0];
        int idx1 = atoi(tokens[4]);
        int idx2 = (n>=6) ? atoi(tokens[5]) : 0;

        char location[64];
        if (typ == 'X') snprintf(location, sizeof(location), "%%%cX%d.%d", area, idx1, idx2);
        else snprintf(location, sizeof(location), "%%%c%c%d", area, typ, idx1);

        void *ptr = NULL; const UA_DataType *uaType = NULL;
        if (!resolvePointerFromLocation(location, &ptr, &uaType)) continue;

        static UA_UInt32 nextId = 4000000;
        UA_NodeId nodeId = UA_NODEID_NUMERIC(g_namespace_index, nextId++);
        addVariableNode(server, dispName, programFolder, nodeId, ptr, (UA_DataType*)uaType);
        added++;
    }
    fclose(f);
    if (added == 0) {
        char log_msg[256];
        sprintf(log_msg, "No located variables found in LOCATED_VARIABLES.h (seen %d macro lines)\n", seen);
        openplc_log(log_msg);
    }
    return added;
}

//-----------------------------------------------------------------------------
// Initialize OPC UA server
//-----------------------------------------------------------------------------
void initializeOpcua() {
    // No-op. We create and configure a fresh server per start to avoid reusing
    // internal open62541 allocations across restarts.
}

//-----------------------------------------------------------------------------
// Finalize OPC UA server
//-----------------------------------------------------------------------------
void finalizeOpcua() {
    // No-op. The server is deleted at the end of opcuaStartServer().
}

//-----------------------------------------------------------------------------
// Stop flag setter for external callers
//-----------------------------------------------------------------------------
void stopOpcua() {
    char log_msg[1000];
    
    if (g_opcua_running) {
        sprintf(log_msg, "Stopping OPC UA server...\n");
        openplc_log(log_msg);
        g_opcua_running = false;
        
        // Give the server a moment to stop gracefully
        usleep(100000); // 100ms
        
        // Force cleanup if server still exists
        if (g_opcua_server != NULL) {
            sprintf(log_msg, "Force cleaning up OPC UA server instance\n");
            openplc_log(log_msg);
            UA_Server_delete(g_opcua_server);
            g_opcua_server = NULL;
        }
    }
}

//-----------------------------------------------------------------------------
// Start OPC UA server
//-----------------------------------------------------------------------------
void opcuaStartServer(int port) {
    char log_msg[1000];
    UA_StatusCode retval;
    
    // Prevent double-start
    if (g_opcua_running) {
        sprintf(log_msg, "OPC UA server already running. Ignoring start request.\n");
        openplc_log(log_msg);
        return;
    }
    
    sprintf(log_msg, "Starting OPC UA server on port %d...\n", port);
    openplc_log(log_msg);
    
    // Clean up any existing server instance first
    if (g_opcua_server != NULL) {
        sprintf(log_msg, "Cleaning up previous OPC UA server instance\n");
        openplc_log(log_msg);
        UA_Server_delete(g_opcua_server);
        g_opcua_server = NULL;
    }
    
    // Reset all state
    g_opcua_running = false;
    g_namespace_index = 1;
    
    // Create a fresh server and configure minimal server with given port
    g_opcua_server = UA_Server_new();
    if (!g_opcua_server) {
        sprintf(log_msg, "Failed to create OPC UA server instance\n");
        openplc_log(log_msg);
        return;
    }

    // Configure server with minimal settings
    sprintf(log_msg, "Configuring server with port %d...\n", port);
    openplc_log(log_msg);
    
    UA_ServerConfig *cfg = UA_Server_getConfig(g_opcua_server);
    if (cfg == NULL) {
        sprintf(log_msg, "Failed to get server config\n");
        openplc_log(log_msg);
        UA_Server_delete(g_opcua_server);
        g_opcua_server = NULL;
        return;
    }
    
    UA_StatusCode configRet = UA_ServerConfig_setMinimal(cfg, (UA_UInt16)port, NULL);
    if (configRet != UA_STATUSCODE_GOOD) {
        sprintf(log_msg, "Failed to configure server: %s\n", UA_StatusCode_name(configRet));
        openplc_log(log_msg);
        UA_Server_delete(g_opcua_server);
        g_opcua_server = NULL;
        return;
    }
    
    sprintf(log_msg, "Server configured successfully\n");
    openplc_log(log_msg);

    // Register namespace first
    registerNamespace(g_opcua_server);
    // Log ABI/runtime info
    logOpen62541AbiInfo(g_opcua_server);
    
    // Scan and create all variable nodes (this will also create folder structure)
    sprintf(log_msg, "About to scan and create nodes...\n");
    openplc_log(log_msg);
    scanAndCreateNodes(g_opcua_server);
    
    sprintf(log_msg, "Node creation completed, setting running flag...\n");
    openplc_log(log_msg);
    g_opcua_running = true;
    
    sprintf(log_msg, "OPC UA server started successfully on port %d (g_opcua_running=%d, g_opcua_server=%p)\n", port, g_opcua_running, (void*)g_opcua_server);
    openplc_log(log_msg);
    
    sprintf(log_msg, "About to start UA_Server_run loop...\n");
    openplc_log(log_msg);
    
    // Use non-blocking server startup
    retval = UA_Server_run_startup(g_opcua_server);
    if (retval != UA_STATUSCODE_GOOD) {
        sprintf(log_msg, "OPC UA server startup failed: %s\n", UA_StatusCode_name(retval));
        openplc_log(log_msg);
        UA_Server_delete(g_opcua_server);
        g_opcua_server = NULL;
        g_opcua_running = false;
        return;
    }
    
    sprintf(log_msg, "OPC UA server startup completed, entering run loop...\n");
    openplc_log(log_msg);
    
    // Non-blocking run loop with longer sleep to allow main thread to run
    while (g_opcua_running) {
        UA_Server_run_iterate(g_opcua_server, true);
        usleep(50000); // 50ms sleep to allow main thread to run
    }
    
    sprintf(log_msg, "OPC UA server stopped\n");
    openplc_log(log_msg);

    // Clean up server instance to ensure clean restarts
    UA_Server_delete(g_opcua_server);
    g_opcua_server = NULL;
    g_opcua_running = false;
}
