//
//  VoodooPS2Elan.cpp
//  VoodooPS2Trackpad
//
//  Created by Kishor Prins on 2020-02-20.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#include "VoodooPS2Elan.h"
#include "../VoodooInput/VoodooInput/VoodooInputMultitouch/VoodooInputMessages.h"

OSDefineMetaClassAndStructors(ApplePS2Elan, IOHIPointing);

bool ApplePS2Elan::init(OSDictionary * properties) {
    if (!super::init(properties)) {
        return false;
    }

    _device = NULL;
    voodooInputInstance = NULL;
    memset(&deviceInfo, 0, sizeof(elantech_device_info));

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
    if (!elantechDetect()) {
        return NULL;
    }

    // get the hardware capabilities
    if (!elantechQueryInfo()) {
        return NULL;
    }

    return this;
}

bool ApplePS2Elan::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

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
    //Linux way of Sending Command

    //Generic Style checking
    request.commands[cmdIndex].command  = kPS2C_SendMouseCommandAndCompareAck;
    request.commands[cmdIndex].inOrOut  = kDP_SetMouseScaling1To1;
    cmdIndex++;

    //Synaptics Style Checking
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
    _device->submitRequestAndBlock(&request);

    //Reading the Version details from the ports
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
    _device->submitRequestAndBlock(&request);

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

    _device->submitRequestAndBlock(&request);

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

bool ApplePS2Elan::elantechSetAbsoluteMode() {
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
