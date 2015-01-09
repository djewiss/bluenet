/**
 * Author: Dominik Egger
 * Copyright: Distributed Organisms B.V. (DoBots)
 * Date: Oct 21, 2014
 * License: LGPLv3+, Apache License, or MIT, your choice
 */


//#include <cmath> // try not to use this!
#include <cstdio>

#include <services/IndoorLocalisationService.h>
#include <common/config.h>
#include <common/boards.h>
#include <drivers/nrf_adc.h>
#include <drivers/nrf_rtc.h>

#include "nRF51822.h"

//#include <common/timer.h>

using namespace BLEpp;

IndoorLocalizationService::IndoorLocalizationService(Nrf51822BluetoothStack& _stack) :
		_stack(&_stack),
		_rssiCharac(NULL), _peripheralCharac(NULL), _scanResult(NULL) {

	setUUID(UUID(INDOORLOCALISATION_UUID));
	//setUUID(UUID(0x3800)); // there is no BLE_UUID for indoor localization (yet)

	// we have to figure out why this goes wrong
	setName(std::string("IndoorLocalizationService"));

	characStatus.reserve(5);

	characStatus.push_back( { "Received signal level",
		RSSI_UUID,
		false,
		static_cast<addCharacteristicFunc>(&IndoorLocalizationService::addSignalStrengthCharacteristic)});
	characStatus.push_back( { "Start/Stop Scan",
		SCAN_DEVICE_UUID,
		false,
		static_cast<addCharacteristicFunc>(&IndoorLocalizationService::addScanControlCharacteristic)});
	characStatus.push_back( { "Peripherals",
		LIST_DEVICE_UUID,
		false,
		static_cast<addCharacteristicFunc>(&IndoorLocalizationService::addPeripheralListCharacteristic)});
	characStatus.push_back( { "List tracked devices",
		TRACKED_DEVICE_LIST_UUID,
		true,
		static_cast<addCharacteristicFunc>(&IndoorLocalizationService::addTrackedDeviceListCharacteristic)});
	characStatus.push_back( { "Add tracked device",
		TRACKED_DEVICE_UUID,
		true,
		static_cast<addCharacteristicFunc>(&IndoorLocalizationService::addTrackedDeviceCharacteristic)});

//	// set timer with compare interrupt every 10ms
//	timer_config(10);
}


void IndoorLocalizationService::addSignalStrengthCharacteristic() {
//	LOGd("create characteristic to read signal strength");
	_rssiCharac = new CharacteristicT<int8_t>();
	_rssiCharac->setUUID(UUID(getUUID(), RSSI_UUID)); // there is no BLE_UUID for rssi level(?)
	_rssiCharac->setName(std::string("Received signal level"));
	_rssiCharac->setDefaultValue(1);
	_rssiCharac->setNotifies(true);

	addCharacteristic(_rssiCharac);
}

void IndoorLocalizationService::addScanControlCharacteristic() {
	// set scanning option
//	LOGd("create characteristic to stop/start scan");
	createCharacteristic<uint8_t>()
		.setUUID(UUID(getUUID(), SCAN_DEVICE_UUID))
		.setName("scan")
		.setDefaultValue(255)
		.setWritable(true)
		.onWrite([&](const uint8_t & value) -> void {
			if(value) {
				LOGi("crown: start scanning");
				if (!_stack->isScanning()) {
					if (_scanResult != NULL) {
						_scanResult->init();
					}
					_stack->startScanning();
				}
			} else {
				LOGi("crown: stop scanning");
				if (_stack->isScanning()) {
					_stack->stopScanning();
					if (_scanResult != NULL) {
						*_peripheralCharac = *_scanResult;
						_scanResult->print();
					}
				}
			}
		});
}

void IndoorLocalizationService::addPeripheralListCharacteristic() {
	// get scan result
//	LOGd("create characteristic to list found peripherals");
	_scanResult = new ScanResult();
	_peripheralCharac = createCharacteristicRef<ScanResult>();
	_peripheralCharac->setUUID(UUID(getUUID(), LIST_DEVICE_UUID));
	_peripheralCharac->setName("Devices");
	_peripheralCharac->setWritable(false);
	_peripheralCharac->setNotifies(true);
}

void IndoorLocalizationService::addTrackedDeviceListCharacteristic() {
	_trackedDeviceListCharac = createCharacteristicRef<TrackedDeviceList>();
	_trackedDeviceListCharac->setUUID(UUID(getUUID(), TRACKED_DEVICE_LIST_UUID));
	_trackedDeviceListCharac->setName("List tracked devices");
	_trackedDeviceListCharac->setWritable(false);
	_trackedDeviceListCharac->setNotifies(false);

	// Add some address and rssi
//	tracked_device_t dev;
//	dev.addr
}

void IndoorLocalizationService::addTrackedDeviceCharacteristic() {
	_trackedDeviceCharac = createCharacteristicRef<TrackedDevice>();
	_trackedDeviceCharac->setUUID(UUID(getUUID(), TRACKED_DEVICE_UUID));
	_trackedDeviceCharac->setName("Add tracked device");
	_trackedDeviceCharac->setWritable(true);
	_trackedDeviceCharac->setNotifies(false);
//	_trackedDeviceCharac->setDefaultValue();
	_trackedDeviceCharac->onWrite([&](const TrackedDevice& value) -> void {
		LOGi("Add tracked device");
		// TODO: actually add it =]
	});
}

IndoorLocalizationService& IndoorLocalizationService::createService(Nrf51822BluetoothStack& _stack) {
	LOGd("Create indoor localisation service");
	IndoorLocalizationService* svc = new IndoorLocalizationService(_stack);
	_stack.addService(svc);
	svc->addSpecificCharacteristics();
	return *svc;
}

void IndoorLocalizationService::on_ble_event(ble_evt_t * p_ble_evt) {
	Service::on_ble_event(p_ble_evt);
	switch (p_ble_evt->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED: {
		sd_ble_gap_rssi_start(p_ble_evt->evt.gap_evt.conn_handle);
		break;
	}
	case BLE_GAP_EVT_DISCONNECTED: {
		sd_ble_gap_rssi_stop(p_ble_evt->evt.gap_evt.conn_handle);
		break;
	}
	case BLE_GAP_EVT_RSSI_CHANGED: {
		onRSSIChanged(p_ble_evt->evt.gap_evt.params.rssi_changed.rssi);
		break;
	}

#if(SOFTDEVICE_SERIES != 110)
	case BLE_GAP_EVT_ADV_REPORT:
		onAdvertisement(&p_ble_evt->evt.gap_evt.params.adv_report);
		break;
#endif

	default: {
	}
	}
}

void IndoorLocalizationService::onRSSIChanged(int8_t rssi) {

#ifdef RGB_LED
	// set LED here
	int sine_index = (rssi - 170) * 2;
	if (sine_index < 0) sine_index = 0;
	if (sine_index > 100) sine_index = 100;
	//			__asm("BKPT");
	//			int sine_index = (rssi % 10) *10;
	PWM::getInstance().setValue(0, sin_table[sine_index]);
	PWM::getInstance().setValue(1, sin_table[(sine_index + 33) % 100]);
	PWM::getInstance().setValue(2, sin_table[(sine_index + 66) % 100]);
	//			counter = (counter + 1) % 100;

	// Add a delay to control the speed of the sine wave
	nrf_delay_us(8000);
#endif

	setRSSILevel(rssi);
}

void IndoorLocalizationService::setRSSILevel(int8_t RSSILevel) {
	if (_rssiCharac) {
		*_rssiCharac = RSSILevel;
	}
}

void IndoorLocalizationService::setRSSILevelHandler(func_t func) {
	_rssiHandler = func;
}

#if(SOFTDEVICE_SERIES != 110)
void IndoorLocalizationService::onAdvertisement(ble_gap_evt_adv_report_t* p_adv_report) {
	if (_stack->isScanning()) {
		if (_scanResult != NULL) {
			_scanResult->update(p_adv_report->peer_addr.addr, p_adv_report->rssi);
		}
		_trackedDeviceList.update(p_adv_report->peer_addr.addr, p_adv_report->rssi);
	}
}
#endif

