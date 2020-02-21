//
//  VoodooPS2Elan.cpp
//  VoodooPS2Trackpad
//
//  Created by Kishor Prins on 2020-02-20.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#include "VoodooPS2Elan.h"
#include "../VoodooInput/VoodooInput/VoodooInputMultitouch/VoodooInputMessages.h"

bool ApplePS2Elan::init(OSDictionary * properties) {
    if (!super::init(properties)) {
        return false;
    }

    return true;
}

ApplePS2Elan* ApplePS2Elan::probe(IOService* provider, SInt32* score) {
    if (!super::probe(provider, score)) {
        return NULL;
    }

    _device = OSDynamicCast(ApplePS2MouseDevice, provider);
    if (!_device) {
        return NULL;
    }

    // send magic knock to the device
    if(!elantechDetect()) {
        return NULL;
    }

    return this;
}

bool ApplePS2Elan::synapticsSendCmd(unsigned char c, unsigned char *param) {
     /* Based on EMlyDinEsHMG's port */
    int cmdIndex = 0;
    PS2Request *request = _device->allocateRequest();
    //Linux way of Sending Command

    //Generic Style checking
    request->commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[cmdIndex].inOrOut  = kDP_SetMouseScaling1To1;
    cmdIndex++;


    //Synaptics Style Checking
    for (int i = 6; i >= 0; i -= 2) {
        unsigned char d = (c >> i) & 3;
        request->commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
        request->commands[cmdIndex].inOrOut  = kDP_SetMouseResolution;
        cmdIndex++;

        request->commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
        request->commands[cmdIndex].inOrOut  = d;
        cmdIndex++;
    }

    request->commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[cmdIndex++].inOrOut  = kDP_GetMouseInformation;
    request->commands[cmdIndex].command = kPS2C_ReadDataPort;
    request->commands[cmdIndex++].inOrOut = 0;
    request->commands[cmdIndex].command = kPS2C_ReadDataPort;
    request->commands[cmdIndex++].inOrOut = 0;
    request->commands[cmdIndex].command = kPS2C_ReadDataPort;
    request->commands[cmdIndex++].inOrOut = 0;

    request->commandsCount = cmdIndex;
    _device->submitRequestAndBlock(request);

    //Reading the Version details from the ports
    param[0] = request->commands[cmdIndex-3].inOrOut;
    param[1] = request->commands[cmdIndex-2].inOrOut;
    param[2] = request->commands[cmdIndex-1].inOrOut;

    _device->freeRequest(request);

    return request->commandsCount != cmdIndex;
}

bool ApplePS2Elan::elantechDetect() {
    /* Based on EMlyDinEsHMG's port */
    PS2Request* request = _device->allocateRequest();

    /*
     * Use magic knock to detect Elantech touchpad
     */
    // Disable stream mode before the command sequence
    request->commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[0].inOrOut  = kDP_SetDefaultsAndDisable;

    request->commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[1].inOrOut  = kDP_SetMouseScaling1To1;
    request->commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[2].inOrOut  = kDP_SetMouseScaling1To1;
    request->commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[3].inOrOut  = kDP_SetMouseScaling1To1;
    //Reading Data
    request->commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request->commands[4].inOrOut  = kDP_GetMouseInformation;
    request->commands[5].command = kPS2C_ReadDataPort;
    request->commands[5].inOrOut = 0;
    request->commands[6].command = kPS2C_ReadDataPort;
    request->commands[6].inOrOut = 0;
    request->commands[7].command = kPS2C_ReadDataPort;
    request->commands[7].inOrOut = 0;
    request->commandsCount = 8;

    _device->submitRequestAndBlock(request);

    //Reading the Version details from the ports
    unsigned char param[3];
    param[0] = request->commands[5].inOrOut;
    param[1] = request->commands[6].inOrOut;
    param[2] = request->commands[7].inOrOut;

    _device->freeRequest(request);

    if (request->commandsCount != 8) {
        IOLog("VoodooPS2Elan: sending Elantech magic knock failed.\nn");
        return false;
    }

    /*
     * Report this in case there are Elantech models that use a different
     * set of magic numbers
     */
    if (param[0] != 0x3c || param[1] != 0x03 ||
        (param[2] != 0xc8 && param[2] != 0x00)) {
        IOLog("VoodooPS2Elan: unexpected magic knock result 0x%02x, 0x%02x, 0x%02x.\n", param[0], param[1], param[2]);
        return false;
    }

    /*
     * Query touchpad's firmware version and see if it reports known
     * value to avoid mis-detection. Logitech mice are known to respond
     * to Elantech magic knock and there might be more.
     */
    memset(param, 0, sizeof(param));
    if (synapticsSendCmd(ETP_FW_VERSION_QUERY, param)) {
        IOLog("VoodooPS2Elan: failed to query firmware version.\n");
        return false;
    }

    IOLog("VoodooPS2ELAN: Elantech version query result 0x%02x, 0x%02x, 0x%02x.\n", param[0], param[1], param[2]);

    if (!elantechIsSignatureValid(param)) {
        IOLog("VoodooPS2ELAN: Probably not a real Elantech touchpad. Aborting.\n");
        return false;
    }


    return true;
}

bool ApplePS2Elan::elantechIsSignatureValid(const unsigned char *param) {
    static const unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10 };
    int i;

    if (param[0] == 0) {
        return false;
    }

    if (param[1] == 0) {
        return true;
    }

    /*
     * Some hw_version >= 4 models have a revision higher then 20. Meaning
     * that param[2] may be 10 or 20, skip the rates check for these.
     */
    if ((param[0] & 0x0f) >= 0x06 && (param[1] & 0xaf) == 0x0f &&
        param[2] < 40)
        return true;

    for (i = 0; i < sizeof(rates); i++) {
        if (param[2] == rates[i]) {
            return false;
        }
    }

    return true;
}

bool ApplePS2Elan::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

    return true;
}

void ApplePS2Elan::stop(IOService* provider) {
    super::stop(provider);
};


bool ApplePS2Elan::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();

        return true;
    }

    return super::handleOpen(forClient, options, arg);
}

void ApplePS2Elan::handleClose(IOService *forClient, IOOptionBits options) {
    OSSafeReleaseNULL(voodooInputInstance);
    super::handleClose(forClient, options);
}
