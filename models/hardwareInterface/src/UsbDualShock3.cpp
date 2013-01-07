#include "UsbDualShock3.hh"
#include "IOException.hh"

#include <stdio.h>
#include <errno.h>
#include <sstream>
#include <cstring>
#include <libusb.h>

using namespace idf;

/**
 * This is required to access fields within struct hid_device in the
 * sendCommand function. hid_device_ is defined in hid.c, which cannot be
 * included here, so we have to replicate some of its definition. It's an ugly
 * hack, but it's the best I have for now.
 */
struct hid_device_ {
    libusb_device_handle *device_handle;
    int a, b, c, interface;
};

UsbDualShock3::UsbDualShock3(int vendorID, int productID) :
    UsbDevice(vendorID, productID) {}

void UsbDualShock3::open() {
    UsbDevice::open();

    /**
     * This device must be sent an HID get report request before it will
     * report events. I found this in a patch to the linux kernel in
     * hid-sony.c. While the device does fill the array with a response, it
     * still doesn't report events until I press the clear PlayStation button
     * on the controller, which worked even before I found this wakeup command.
     * I'm not sure this is necessary or if it's supposed to replace the
     * need to press that button and I'm still missing something, but here it
     * is either way.
     */
    unsigned char wakeup[18];
    wakeup[0] = 0xf2;

    if (hid_get_feature_report(hidDevice, wakeup, sizeof(wakeup)) < 0) {
        std::ostringstream oss;
        oss << __FILE__ << ":" << __LINE__
            << " Failed to send wakeup command: " << strerror(errno);
        throw IOException(oss.str().c_str());
    }
}

void UsbDualShock3::update() {
    UsbDevice::update();

    unsigned char buffer[49];
    int bytesRead;
    bool dataReceived = false;

    do {
        dataReceived |= bytesRead = read(buffer, sizeof(buffer));
    } while (bytesRead > 0);

    if (dataReceived) {
        selectButton.setValue(buffer[2] & 1);
        leftAnalogIn.setValue(buffer[2] >> 1 & 1);
        rightAnalogIn.setValue(buffer[2] >> 2 & 1);
        startButton.setValue(buffer[2] >> 3 & 1);

        playStationButton.setValue(buffer[4] & 1);

        leftAnalogLeftRightPivot.setValue(buffer[6]);
        leftAnalogUpDownPivot.setValue(buffer[7]);
        rightAnalogLeftRightPivot.setValue(buffer[8]);
        rightAnalogUpDownPivot.setValue(buffer[9]);

        directionalPadUp.setValue(buffer[14]);
        directionalPadRight.setValue(buffer[15]);
        directionalPadDown.setValue(buffer[16]);
        directionalPadLeft.setValue(buffer[17]);

        leftTrigger.setValue(buffer[18]);
        rightTrigger.setValue(buffer[19]);
        leftBumper.setValue(buffer[20]);
        rightBumper.setValue(buffer[21]);

        triangleButton.setValue(buffer[22]);
        circleButton.setValue(buffer[23]);
        xButton.setValue(buffer[24]);
        squareButton.setValue(buffer[25]);
    }
}

void UsbDualShock3::sendCommand() {
    if (!mOpen) {
        std::ostringstream oss;
        oss << __FILE__ << ":" << __LINE__
            << " Device is not open.";
        throw IOException(oss.str().c_str());
    }

    /**
     * This devices incorrectly requires that commands be sent over the control
     * endpoint, so we have to call libusb directly rather than use hidapi,
     * which would use the interrupt out endpoint.
     */
    int result = libusb_control_transfer(hidDevice->device_handle,
      LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
      0x09 /* HID set report */, (2 /* HID output */ << 8) | command[0],
      hidDevice->interface, command, sizeof(command), 1000);

    if (result < 0) {
        std::ostringstream oss;
        oss << __FILE__ << ":" << __LINE__
            << " Transfer failed with LIBUSB_ERROR code " << result;
        throw IOException(oss.str().c_str());
    }
}