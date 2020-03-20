//
//  VoodooPS2Elan.cpp
//  VoodooPS2Trackpad
//
//  Created by Kishor Prins on 2020-02-20.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#include "VoodooPS2Elan.h"
#define kIOFBTransformKey               "IOFBTransform"
#include "../VoodooInput/VoodooInput/VoodooInputMultitouch/VoodooInputMessages.h"

OSDefineMetaClassAndStructors(ApplePS2Elan, IOHIPointing);

bool ApplePS2Elan::init(OSDictionary * properties) {
    if (!super::init(properties)) {
        return false;
    }

    device = NULL;
    voodooInputInstance = NULL;
    memset(&deviceData, 0, sizeof(elantech_data));
    memset(&deviceInfo, 0, sizeof(elantech_device_info));
    packetByteCount = 0;

    return true;
}

ApplePS2Elan* ApplePS2Elan::probe(IOService* provider, SInt32* score) {
    if (!super::probe(provider, score)) {
        return NULL;
    }

    device = OSDynamicCast(ApplePS2MouseDevice, provider);
    if (!device) {
        return NULL;
    }

    // send magic knock to the device
    if (!elantechDetect()) {
        return NULL;
    }

    return this;
}

bool ApplePS2Elan::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

    // get the hardware capabilities
    if (!elantechQueryInfo()) {
        return false;
    }

    // setup the ps2 parameters
    if (!elantechSetupPS2()) {
        return false;
    }

    // Ported from Emlydinesh
    setSampleRateAndResolution();
    getMouseInformation();


    device->installInterruptAction(this, OSMemberFunctionCast(PS2InterruptAction ,this, &ApplePS2Elan::interruptOccurred), OSMemberFunctionCast(PS2PacketAction, this, &ApplePS2Elan::packetReady));


    return true;
}

void ApplePS2Elan::stop(IOService* provider) {
    super::stop(provider);
}

bool ApplePS2Elan::sendCmd(unsigned char c, unsigned char *param) {
    if (deviceInfo.use_elan_cmd) {
        return ApplePS2Elan::elantechSendCmd(c, param);
    }

    return ApplePS2Elan::synapticsSendCmd(c, param);
}

bool ApplePS2Elan::synapticsSendCmd(unsigned char c, unsigned char *param) {
    /* Based on EMlyDinEsHMG's port */
    int cmdIndex = 0;
    TPS2Request<> request;

    request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdIndex].inOrOut  = kDP_SetMouseScaling1To1;
    cmdIndex++;

    for (int i = 6; i >= 0; i -= 2) {
        unsigned char d = (c >> i) & 3;
        request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmdIndex].inOrOut  = kDP_SetMouseResolution;
        cmdIndex++;

        request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmdIndex].inOrOut  = d;
        cmdIndex++;
    }

    request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdIndex++].inOrOut  = kDP_GetMouseInformation;
    request.commands[cmdIndex].command = kPS2C_ReadDataPort;
    request.commands[cmdIndex++].inOrOut = 0;
    request.commands[cmdIndex].command = kPS2C_ReadDataPort;
    request.commands[cmdIndex++].inOrOut = 0;
    request.commands[cmdIndex].command = kPS2C_ReadDataPort;
    request.commands[cmdIndex++].inOrOut = 0;

    request.commandsCount = cmdIndex;
    device->submitRequestAndBlock(&request);

    param[0] = request.commands[cmdIndex-3].inOrOut;
    param[1] = request.commands[cmdIndex-2].inOrOut;
    param[2] = request.commands[cmdIndex-1].inOrOut;

    if (request.commandsCount != cmdIndex) {
        IOLog("VoodooPS2Elan: synapticsSendCmd query 0x%02x failed.\n", c);
        return false;
    }

    return true;
}

bool ApplePS2Elan::elantechSendCmd(unsigned char c, unsigned char *param) {
    /* Based on EMlyDinEsHMG's port */
    TPS2Request<> request;
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = ETP_PS2_CUSTOM_COMMAND;
    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = c;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = kDP_GetMouseInformation;
    request.commands[3].command = kPS2C_ReadDataPort;
    request.commands[3].inOrOut = 0;
    request.commands[4].command = kPS2C_ReadDataPort;
    request.commands[4].inOrOut = 0;
    request.commands[5].command = kPS2C_ReadDataPort;
    request.commands[5].inOrOut = 0;
    request.commandsCount = 6;
    device->submitRequestAndBlock(&request);

    //Reading the Version details from the ports
    param[0] = request.commands[3].inOrOut;
    param[1] = request.commands[4].inOrOut;
    param[2] = request.commands[5].inOrOut;

    if (request.commandsCount != 6) {
        IOLog("VoodooPS2Elan: elantechSendCmd query 0x%02x failed.\n", c);
        return false;
    }

    return true;
}

bool ApplePS2Elan::ps2SlicedCommand(unsigned char c) {
    /* Based on EMlyDinEsHMG's port */
    int cmdIndex = 0;
    TPS2Request<> request;

    request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdIndex].inOrOut  = kDP_SetMouseScaling1To1;
    cmdIndex++;

    for (int i = 6; i >= 0; i -= 2) {
        unsigned char d = (c >> i) & 3;
        request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmdIndex].inOrOut  = kDP_SetMouseResolution;
        cmdIndex++;

        request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[cmdIndex].inOrOut  = d;
        cmdIndex++;
    }

    request.commandsCount = cmdIndex;
    device->submitRequestAndBlock(&request);

    return request.commandsCount == cmdIndex;
}

bool ApplePS2Elan::genericPS2Cmd(unsigned char *param, unsigned char c) {
    /* Based on EMlyDinEsHMG's port */
    TPS2Request<> request;

    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = c;
    request.commandsCount = 1;

    if (c == kDP_GetMouseInformation)
    {
        request.commands[1].command = kPS2C_ReadDataPort;
        request.commands[1].inOrOut = 0;
        request.commands[2].command = kPS2C_ReadDataPort;
        request.commands[2].inOrOut = 0;
        request.commands[3].command = kPS2C_ReadDataPort;
        request.commands[3].inOrOut = 0;
        request.commandsCount = 4;
    }

    device->submitRequestAndBlock(&request);

    if (c == kDP_GetMouseInformation)
    {
        //Reading the Version details from the ports
        param[0] = request.commands[1].inOrOut;
        param[1] = request.commands[2].inOrOut;
        param[2] = request.commands[3].inOrOut;

        return request.commandsCount == 4;
    } else {
        return request.commandsCount == 1;
    }
}

bool ApplePS2Elan::elantechPS2Cmd(unsigned char *param, unsigned char c) {
    /* Based on EMlyDinEsHMG's port */
    int rc = -1;
    int tries = ETP_PS2_COMMAND_TRIES;

    TPS2Request<> request;
    do {
        request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
        request.commands[0].inOrOut  = c;
        request.commandsCount = 1;

        if (c == kDP_GetMouseInformation) {
            request.commands[1].command = kPS2C_ReadDataPort;
            request.commands[1].inOrOut = 0;
            request.commands[2].command = kPS2C_ReadDataPort;
            request.commands[2].inOrOut = 0;
            request.commands[3].command = kPS2C_ReadDataPort;
            request.commands[3].inOrOut = 0;
            request.commandsCount = 4;
        }

        device->submitRequestAndBlock(&request);

        if (c == kDP_GetMouseInformation) {
            param[0] = request.commands[1].inOrOut;
            param[1] = request.commands[2].inOrOut;
            param[2] = request.commands[3].inOrOut;

            if(request.commandsCount == 4) {
                rc = 0;
            } else {
                if (request.commandsCount == 1) {
                    rc = 0;
                }
            }

            if (rc == 0) {
                break;
            }

            tries--;

            IOSleep(ETP_PS2_COMMAND_DELAY);
        }
    } while (tries > 0);

    if (rc != 0) {
        IOLog("VoodooPS2Elan: ps2 command 0x%02x failed.\n", c);
    }

    return rc == 0;
}

bool ApplePS2Elan::elantechWriteReg(unsigned char reg, unsigned char *val) {
    int rc = 0;
    if ((reg < 0x07 || reg > 0x26) || (reg > 0x11 && reg < 0x20)) {
        return false;
    }

    switch (deviceInfo.hw_version) {
        case 1:
            if (ps2SlicedCommand(ETP_REGISTER_WRITE) ||
                ps2SlicedCommand(reg) ||
                ps2SlicedCommand(*val) ||
                genericPS2Cmd(NULL, kDP_SetMouseScaling1To1)) {
                rc = -1;
            }
            break;

        case 2:
            if (elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, ETP_REGISTER_WRITE) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, reg) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, *val) ||
                elantechPS2Cmd(NULL, kDP_SetMouseScaling1To1)) {
                rc = -1;
            }
            break;

        case 3:
            if (elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, ETP_REGISTER_READWRITE) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, reg) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, *val) ||
                elantechPS2Cmd(NULL, kDP_SetMouseScaling1To1)) {
                rc = -1;
            }
            break;

        case 4:
            if (elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, ETP_REGISTER_READWRITE) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, reg) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, ETP_REGISTER_READWRITE) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, *val) ||
                elantechPS2Cmd(NULL, kDP_SetMouseScaling1To1)) {
                rc = -1;
            }
            break;
    }

    if (rc) {
        IOLog("VoodooPS2Elan: Failed to write register 0x%02x with value 0x%02x.\n", reg, *val);
    }

    return rc == 0;
}

bool ApplePS2Elan::elantechReadReg(unsigned char reg, unsigned char *val) {
    unsigned char param[3];
    int rc = 0;

    if ((reg < 0x07 || reg > 0x26) || (reg > 0x11 && reg < 0x20)) {
        return false;
    }

    switch (deviceInfo.hw_version) {
        case 1:
            if (ps2SlicedCommand(ETP_REGISTER_READ) ||
                ps2SlicedCommand(reg) ||
                genericPS2Cmd(param, kDP_GetMouseInformation)) {
                rc = -1;
            }
            break;

        case 2:
            if (elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, ETP_REGISTER_READ) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, reg) ||
                elantechPS2Cmd(param, kDP_GetMouseInformation)) {
                rc = -1;
            }
            break;

        case 3 ... 4:
            if (elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, ETP_REGISTER_READWRITE) ||
                elantechPS2Cmd(NULL, ETP_PS2_CUSTOM_COMMAND) ||
                elantechPS2Cmd(NULL, reg) ||
                elantechPS2Cmd(param, kDP_GetMouseInformation)) {
                rc = -1;
            }
            break;
    }

    if (rc) {
        IOLog("VoodooPS2Elan: failed to read register 0x%02x.\n", reg);
    } else if (deviceInfo.hw_version != 4) {
        *val = param[0];
    } else {
        *val = param[1];
    }

    return rc == 0;
}

bool ApplePS2Elan::elantechDetect() {
    /* Based on EMlyDinEsHMG's port */
    TPS2Request<8> request;
    /*
     * Use magic knock to detect Elantech touchpad
     */
    // Disable stream mode before the command sequence
    request.commands[0].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut  = kDP_SetDefaultsAndDisable;

    request.commands[1].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut  = kDP_SetMouseScaling1To1;
    request.commands[2].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut  = kDP_SetMouseScaling1To1;
    request.commands[3].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut  = kDP_SetMouseScaling1To1;
    //Reading Data
    request.commands[4].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut  = kDP_GetMouseInformation;
    request.commands[5].command = kPS2C_ReadDataPort;
    request.commands[5].inOrOut = 0;
    request.commands[6].command = kPS2C_ReadDataPort;
    request.commands[6].inOrOut = 0;
    request.commands[7].command = kPS2C_ReadDataPort;
    request.commands[7].inOrOut = 0;
    request.commandsCount = 8;

    device->submitRequestAndBlock(&request);

    //Reading the Version details from the ports
    unsigned char param[3];
    param[0] = request.commands[5].inOrOut;
    param[1] = request.commands[6].inOrOut;
    param[2] = request.commands[7].inOrOut;

    if (request.commandsCount != 8) {
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
    if (!synapticsSendCmd(ETP_FW_VERSION_QUERY, param)) {
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

bool ApplePS2Elan::elantechQueryInfo() {
    unsigned char param[3] = { 0, 0, 0 };
    unsigned char traces = 0;

    if (!synapticsSendCmd(ETP_FW_VERSION_QUERY, param)) {
        IOLog("VoodooPS2ELAN: failed to query firmware version.\n");
        return false;
    }

    deviceInfo.fw_version = (param[0] << 16) | (param[1] << 8) | param[2];

    if (!elantechSetProperties()) {
        IOLog("VoodooPS2ELAN: unknown hardware version, aborting...\n");
        return false;
    }

    IOLog("VoodooPS2ELAN: assuming hardware version %d (with firmware version 0x%02x%02x%02x)\n", deviceInfo.hw_version, param[0], param[1], param[2]);

    if (!sendCmd(ETP_CAPABILITIES_QUERY, deviceInfo.capabilities)) {
        IOLog("VoodooPS2ELAN: failed to query capabilities.\n");
        return false;
    }

    IOLog("VoodooPS2Elan: Synaptics capabilities query result 0x%02x, 0x%02x, 0x%02x.\n", deviceInfo.capabilities[0], deviceInfo.capabilities[1], deviceInfo.capabilities[2]);

    if (deviceInfo.hw_version != 1) {
        if (!sendCmd(ETP_SAMPLE_QUERY, deviceInfo.samples)) {
            IOLog("VoodooPS2Elan: failed to query sample data\n");
            return false;
        }
        IOLog("VoodooPS2Elan: Elan sample query result %02x, %02x, %02x\n", deviceInfo.samples[0], deviceInfo.samples[1], deviceInfo.samples[2]);
    }

    if (deviceInfo.samples[1] == 0x74 && deviceInfo.hw_version == 0x03) {
        /*
         * This module has a bug which makes absolute mode
         * unusable, so let's abort so we'll be using standard
         * PS/2 protocol.
         */
        IOLog("VoodooPS2Elan: absolute mode broken, forcing standard PS/2 protocol\n");
        return false;
    }

    /* The MSB indicates the presence of the trackpoint */
    deviceInfo.has_trackpoint = (deviceInfo.capabilities[0] & 0x80) == 0x80;

    deviceInfo.x_res = 31;
    deviceInfo.y_res = 31;
    if (deviceInfo.hw_version == 4) {
        if (!elantechGetResolutionV4()) {
            IOLog("VoodooPS2Elan: failed to query resolution data.\n");
        }
    }

    /* query range information */
    switch (deviceInfo.hw_version) {
        case 1:
            deviceInfo.x_min = ETP_XMIN_V1;
            deviceInfo.y_min = ETP_YMIN_V1;
            deviceInfo.x_max = ETP_XMAX_V1;
            deviceInfo.y_max = ETP_YMAX_V1;
            break;

        case 2:
            if (deviceInfo.fw_version == 0x020800 ||
                 deviceInfo.fw_version == 0x020b00 ||
                 deviceInfo.fw_version == 0x020030) {
                 deviceInfo.x_min = ETP_XMIN_V2;
                 deviceInfo.y_min = ETP_YMIN_V2;
                 deviceInfo.x_max = ETP_XMAX_V2;
                 deviceInfo.y_max = ETP_YMAX_V2;
            } else {
                int i;
                int fixed_dpi;

                i = ( deviceInfo.fw_version > 0x020800 &&
                      deviceInfo.fw_version < 0x020900) ? 1 : 2;

                if (!sendCmd(ETP_FW_ID_QUERY, param)) {
                    return false;
                }

                fixed_dpi = param[1] & 0x10;

                if ((( deviceInfo.fw_version >> 16) == 0x14) && fixed_dpi) {
                    if (!sendCmd(ETP_SAMPLE_QUERY, param)) {
                        return false;
                    }

                     deviceInfo.x_max = ( deviceInfo.capabilities[1] - i) * param[1] / 2;
                     deviceInfo.y_max = ( deviceInfo.capabilities[2] - i) * param[2] / 2;
                } else if ( deviceInfo.fw_version == 0x040216) {
                     deviceInfo.x_max = 819;
                     deviceInfo.y_max = 405;
                } else if ( deviceInfo.fw_version == 0x040219 ||  deviceInfo.fw_version == 0x040215) {
                     deviceInfo.x_max = 900;
                     deviceInfo.y_max = 500;
                } else {
                     deviceInfo.x_max = ( deviceInfo.capabilities[1] - i) * 64;
                     deviceInfo.y_max = ( deviceInfo.capabilities[2] - i) * 64;
                }
            }
            break;

        case 3:
            if (!sendCmd(ETP_FW_ID_QUERY, param))
                return false;

            deviceInfo.x_max = (0x0f & param[0]) << 8 | param[1];
            deviceInfo.y_max = (0xf0 & param[0]) << 4 | param[2];
            break;

        case 4:
            if (!sendCmd(ETP_FW_ID_QUERY, param))
                return false;

             deviceInfo.x_max = (0x0f & param[0]) << 8 | param[1];
             deviceInfo.y_max = (0xf0 & param[0]) << 4 | param[2];
            traces =  deviceInfo.capabilities[1];
            if ((traces < 2) || (traces >  deviceInfo.x_max))
                return false;

             deviceInfo.width =  deviceInfo.x_max / (traces - 1);

            /* column number of traces */
             deviceInfo.x_traces = traces;

            /* row number of traces */
            traces =  deviceInfo.capabilities[2];
            if ((traces >= 2) && (traces <=  deviceInfo.y_max))
                 deviceInfo.y_traces = traces;

            break;
    }

    /* check for the middle button: DMI matching or new v4 firmwares */
    /*info->has_middle_button = dmi_check_system(elantech_dmi_has_middle_button) ||
                  (ETP_NEW_IC_SMBUS_HOST_NOTIFY(info->fw_version) &&
                   !elantech_is_buttonpad(info)); */

    return true;
}

/*
* determine hardware version and set some properties according to it.
*/
bool ApplePS2Elan::elantechSetProperties() {
    /* This represents the version of IC body. */
    int ver = (deviceInfo.fw_version & 0x0f0000) >> 16;

    /* Early version of Elan touchpads doesn't obey the rule. */
    if (deviceInfo.fw_version < 0x020030 || deviceInfo.fw_version == 0x020600) {
        deviceInfo.hw_version = 1;
    } else {
        switch (ver) {
            case 2:
            case 4:
                deviceInfo.hw_version = 2;
                break;
            case 5:
                deviceInfo.hw_version = 3;
                break;
            case 6 ... 15:
                deviceInfo.hw_version = 4;
                break;
            default:
                return false;
        }
    }

    /* decide which send_cmd we're gonna use early */
    deviceInfo.use_elan_cmd = deviceInfo.hw_version >= 3;

    /* Turn on packet checking by default */
    deviceInfo.paritycheck = 1;

    /*
     * This firmware suffers from misreporting coordinates when
     * a touch action starts causing the mouse cursor or scrolled page
     * to jump. Enable a workaround.
     */
    deviceInfo.jumpy_cursor = (deviceInfo.fw_version == 0x020022 || deviceInfo.fw_version == 0x020600);

    if (deviceInfo.hw_version > 1) {
        /* For now show extra debug information */
        deviceInfo.debug = 1;

        if (deviceInfo.fw_version >= 0x020800)
            deviceInfo.reports_pressure = true;
    }

    /*
     * The signatures of v3 and v4 packets change depending on the
     * value of this hardware flag.
     */
    deviceInfo.crc_enabled = (deviceInfo.fw_version & 0x4000) == 0x4000;

    /* Enable real hardware resolution on hw_version 3 ? */
    // deviceInfo.set_hw_resolution = !dmi_check_system(no_hw_res_dmi_table);

    return true;
}

bool ApplePS2Elan::elantechGetResolutionV4() {
    unsigned char param[3] = { 0, 0, 0};

    if (!sendCmd(ETP_RESOLUTION_QUERY, param)) {
        return false;
    }

    deviceInfo.x_res = elantechConvertRes(param[1] & 0x0f);
    deviceInfo.y_res = elantechConvertRes((param[1] & 0xf0) >> 4);
    deviceInfo.bus = param[2];

    return true;
}

/*
 * (value from firmware) * 10 + 790 = dpi
 * we also have to convert dpi to dots/mm (*10/254 to avoid floating point)
 */
unsigned int ApplePS2Elan::elantechConvertRes(unsigned int val)
{
    return (val * 10 + 790) * 10 / 254;
}

/*
* Initialize the touchpad and create sysfs entries
*/
bool ApplePS2Elan::elantechSetupPS2() {
    deviceData.parity[0] = 1;
    for (int i = 1; i < 256; i++) {
        deviceData.parity[i] = deviceData.parity[i & (i - 1)] ^ 1;
    }

    if (!elantechSetAbsoluteMode()) {
        IOLog("VoodooPS2Elan: failed to put touchpad into absolute mode.\n");
        return false;
    }

    if (!elantechSetInputParams()) {
         IOLog("VoodooPS2Elan: failed to query touchpad range.\n");
    }

    return true;
}

/*
* Put the touchpad into absolute mode
*/
bool ApplePS2Elan::elantechSetAbsoluteMode() {
    unsigned char val = 0;
    int tries = ETP_READ_BACK_TRIES;
    int rc = 0;

    /* Based on EMlyDinEsHMG's port */
    // Reset the trackpad
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_Reset;                          // 0xFF
    request.commandsCount = 1;
    device->submitRequestAndBlock(&request);

    switch (deviceInfo.hw_version) {
        case 1:
            deviceData.reg_10 = 0x16;
            deviceData.reg_11 = 0x8f;
            if (elantechWriteReg(0x10, &deviceData.reg_10) ||
                elantechWriteReg(0x11, &deviceData.reg_11)) {
                rc = -1;
            }
            break;

        case 2:
            /* Windows driver values */
            deviceData.reg_10 = 0x54;
            deviceData.reg_11 = 0x88;    /* 0x8a */
            deviceData.reg_21 = 0x60;    /* 0x00 */
            if (elantechWriteReg(0x10, &deviceData.reg_10) ||
                elantechWriteReg(0x11, &deviceData.reg_11) ||
                elantechWriteReg(0x21, &deviceData.reg_21)) {
                rc = -1;
            }
            break;

        case 3:
            if (deviceInfo.set_hw_resolution)
                deviceData.reg_10 = 0x0b;
            else
                deviceData.reg_10 = 0x01;

            if (elantechWriteReg(0x10, &deviceData.reg_10))
                rc = -1;

            break;

        case 4:
            deviceData.reg_07 = 0x01;
            if (elantechWriteReg(0x07, &deviceData.reg_07))
                rc = -1;

            /* v4 has no reg 0x10 to read */
            goto skip_readback_reg_10;
    }

    if (rc == 0) {
        /*
         * Read back reg 0x10. For hardware version 1 we must make
         * sure the absolute mode bit is set. For hardware version 2
         * the touchpad is probably initializing and not ready until
         * we read back the value we just wrote.
         */
        do {
            rc = elantechReadReg(0x10, &val);
            if (rc == 0)
                break;
            tries--;
            IOSleep(ETP_READ_BACK_DELAY);
        } while (tries > 0);

        if (rc) {
            IOLog("VoodooPS2Elan: failed to read back register 0x10.\n");
        } else if (deviceInfo.hw_version == 1 && !(val & ETP_R10_ABSOLUTE_MODE)) {
            IOLog("VoodooPS2Elan: touchpad refuses to switch to absolute mode.\n");
            rc = -1;
        }
    }

skip_readback_reg_10:
    if (rc) {
        IOLog("VoodooPS2Elan: failed to initialise registers.\n");
        return false;
    }

    return rc == 0;
}


void ApplePS2Elan::setSampleRateAndResolution() {
    TPS2Request<> request;
    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_SetDefaultsAndDisable;           // 0xF5, Disable data reporting
    request.commands[1].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[1].inOrOut = kDP_SetMouseSampleRate;              // 0xF3
    request.commands[2].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[2].inOrOut = 0x64;                                // 100 dpi
    request.commands[3].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[3].inOrOut = kDP_SetMouseResolution;              // 0xE8
    request.commands[4].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[4].inOrOut = 0x03;                                // 0x03 = 8 counts/mm
    request.commands[5].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[5].inOrOut = kDP_SetMouseScaling1To1;             // 0xE6
    request.commands[6].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[6].inOrOut = kDP_Enable;                          // 0xF4, Enable Data Reporting
    request.commandsCount = 7;

    device->submitRequestAndBlock(&request);
}

void ApplePS2Elan::getMouseInformation() {
    TPS2Request<> request;

    request.commands[0].command = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[0].inOrOut = kDP_GetMouseInformation;
    request.commands[1].command = kPS2C_ReadDataPort;
    request.commands[1].inOrOut = 0;
    request.commands[2].command = kPS2C_ReadDataPort;
    request.commands[2].inOrOut = 0;
    request.commands[3].command = kPS2C_ReadDataPort;
    request.commands[3].inOrOut = 0;
    request.commandsCount = 4;
    
    device->submitRequestAndBlock(&request);
}

/*
* Set the appropriate event bits for the input subsystem
*/
bool ApplePS2Elan::elantechSetInputParams() {
    // Set up the VoodooInput interface
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, deviceInfo.x_max - deviceInfo.x_min, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, deviceInfo.y_max - deviceInfo.y_min, 32);

    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, (deviceInfo.x_max + 1) / deviceInfo.x_res, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, (deviceInfo.y_max + 1) / deviceInfo.y_res, 32);

    setProperty(kIOFBTransformKey, 0ull, 32);
    setProperty("VoodooInputSupported", kOSBooleanTrue);

    return true;
}

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


PS2InterruptResult ApplePS2Elan::interruptOccurred(UInt8 data) {
    UInt8* packet = ringBuffer.head();
    packet[packetByteCount++] = data;

    if (packetByteCount == kPacketLengthMax)
    {
        ringBuffer.advanceHead(kPacketLengthMax);
        packetByteCount = 0;
        return kPS2IR_packetReady;
    }

    return kPS2IR_packetBuffering;
}

/*
* Process byte stream from mouse and handle complete packets
*/
void ApplePS2Elan::packetReady()
{
    // empty the ring buffer, dispatching each packet...
    while (ringBuffer.count() >= kPacketLength)
    {
        int packetType;
        switch (deviceInfo.hw_version) {
            case 1:
            case 2:
                // V1 and V2 are ancient hardware, not going to implement right away
                break;
            case 3:
                break;
            case 4:
                packetType = elantechPacketCheckV4();

                switch (packetType) {
                    case PACKET_UNKNOWN:
                        break;

                    case PACKET_TRACKPOINT:
                        break;
                    default:
                        elantechReportAbsoluteV4(packetType);
                }
                break;
        }

        ringBuffer.advanceTail(kPacketLength);
    }
}

int ApplePS2Elan::elantechPacketCheckV4() {
    unsigned char *packet = ringBuffer.tail();
    unsigned char packetType = packet[3] & 0x03;
    unsigned int icVersion;
    bool sanityCheck;

    if (deviceData.tp_dev && (packet[3] & 0x0f) == 0x06) {
        return PACKET_TRACKPOINT;
    }

    /* This represents the version of IC body. */
    icVersion = (deviceInfo.fw_version & 0x0f0000) >> 16;

    /*
    * Sanity check based on the constant bits of a packet.
    * The constant bits change depending on the value of
    * the hardware flag 'crc_enabled' and the version of
    * the IC body, but are the same for every packet,
    * regardless of the type.
    */
    if (deviceInfo.crc_enabled) {
        sanityCheck = ((packet[3] & 0x08) == 0x00);
    } else if (icVersion == 7 && deviceInfo.samples[1] == 0x2A) {
        sanityCheck = ((packet[3] & 0x1c) == 0x10);
    } else {
        sanityCheck = ((packet[0] & 0x08) == 0x00 && (packet[3] & 0x1c) == 0x10);
    }

    if (!sanityCheck) {
        return PACKET_UNKNOWN;
    }

    switch (packetType) {
        case 0:
            return PACKET_V4_STATUS;
        case 1:
            return PACKET_V4_HEAD;
        case 2:
            return PACKET_V4_MOTION;
    }

    return PACKET_UNKNOWN;
}

void ApplePS2Elan::elantechReportAbsoluteV4(int packetType) {
    switch (packetType) {
        case PACKET_V4_STATUS:
            processPacketStatusV4();
            break;

        case PACKET_V4_HEAD:
            processPacketHeadV4();
            break;

        case PACKET_V4_MOTION:
            processPacketMotionV4();
            break;
        case PACKET_UNKNOWN:
        default:
            /* impossible to get here */
            break;
    }
}


void ApplePS2Elan::processPacketStatusV4() {
    unsigned char *packet = ringBuffer.tail();
    unsigned fingers;
    int i;

    /* notify finger state change */
    fingers = packet[1] & 0x1f;
    for (i = 0; i < ETP_MAX_FINGERS; i++) {
        if ((fingers & (1 << i)) == 0) {
            // finger has been lifted off the touchpad
        }
    }

    elantechInputSyncV4();
}


void ApplePS2Elan::processPacketHeadV4() {
    unsigned char *packet = ringBuffer.tail();
    int id = ((packet[3] & 0xe0) >> 5) - 1;
    int pres, traces;

    if (id < 0) {
        return;
    }

    deviceData.mt[id].x = ((packet[1] & 0x0f) << 8) | packet[2];
    deviceData.mt[id].y = deviceData.y_max - (((packet[4] & 0x0f) << 8) | packet[5]);
    pres = (packet[1] & 0xf0) | ((packet[4] & 0xf0) >> 4);
    traces = (packet[0] & 0xf0) >> 4;

    elantechInputSyncV4();
}

void ApplePS2Elan::processPacketMotionV4() {
    unsigned char *packet = ringBuffer.tail();
    int weight, delta_x1 = 0, delta_y1 = 0, delta_x2 = 0, delta_y2 = 0;
    int id, sid;

    id = ((packet[0] & 0xe0) >> 5) - 1;
    if (id < 0) {
        return;
    }

    sid = ((packet[3] & 0xe0) >> 5) - 1;
    weight = (packet[0] & 0x10) ? ETP_WEIGHT_VALUE : 1;
    /*
     * Motion packets give us the delta of x, y values of specific fingers,
     * but in two's complement. Let the compiler do the conversion for us.
     * Also _enlarge_ the numbers to int, in case of overflow.
     */
    delta_x1 = (signed char)packet[1];
    delta_y1 = (signed char)packet[2];
    delta_x2 = (signed char)packet[4];
    delta_y2 = (signed char)packet[5];

    deviceData.mt[id].x += delta_x1 * weight;
    deviceData.mt[id].y -= delta_y1 * weight;

    if (sid >= 0) {
        deviceData.mt[sid].x += delta_x2 * weight;
        deviceData.mt[sid].y -= delta_y2 * weight;
    }

    elantechInputSyncV4();
}

void ApplePS2Elan::elantechInputSyncV4() {
    // handle physical buttons here
}
