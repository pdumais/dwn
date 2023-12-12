# TODO:  
    prov
        when scanning, the whole thing blocks. And I cant even ping anymore
    on webserver_stop, must delete the async workers

    add websocket in server
    Use http async handlers
    add websocket client
    
    AP list not showing up, and old config either
        Should be fixed now

    outgoing websocket connection
    webpage
        implement all API calls

    Change UDP magic for OTA to a rest command
    what to monitor on nodes?
        temperature sensor?
        List of AP?
        light sensor?
        atmospheric pressure?



    pcb with all the switches and terminals. Then replace garage door controller
        wroom32 socket
        screw terminals
        3 LEDs
        space for a relay?
        DIN mount holes
        1 button for prov
        reset button


# Enhancements
    PROV
        WAP webpage: 
            - stay in prov mode if bad info was sent and we cant connect to WAP
              ask if wanna preserve those changes or not
              - if provisioned, switch to STA only
              - wait for prov info
              - try to connect to ssid. if fail then show err
              - if success, switch to STA only


# Create partition table
    ensure you have "CONFIG_PARTITION_TABLE_TWO_OTA=y" in sdkconfig
    docker run -v /dev/ttyUSB9:/dev/ttyUSB0 --rm -it --privileged -v $(pwd):/build lpodkalicki/esp32-toolchain idf.py partition_table-flash -p /dev/ttyUSB0

# Build:
    docker run --rm -it --privileged -v $(pwd):/build -u $UID -w /build -e HOME=/tmp espressif/idf:latest idf.py build

# Flash
    docker run -v /dev/ttyUSB9:/dev/ttyUSB0 --rm -it --privileged -v $(pwd):/build -w /build -e HOME=/tmp espressif/idf:latest idf.py flash monitor -p /dev/ttyUSB0

# Erase NVS (to un-provision)
    docker run -v /dev/ttyUSB9:/dev/ttyUSB0 --rm -it --privileged -v $(pwd):/build -w /build -e HOME=/tmp espressif/idf:latest parttool.py -p /dev/ttyUSB0 erase_partition --partition-name=nvs

# Wifi Provision:
When booting the device, it will enter provisioning mode if any of the following condition is met:

- booting the device while pressing the provisioning button (pin 22), holding it to ground.
- No wifi config is found in NVS storage (new device or NVS has been erased)

During provisioning mode, the device will show up as a AP. Use a mobile device or laptop to connect to that AP and go to http://192.168.4.1.
The device will present a page where you can enter a SSID, password and static IP. All fields are mandatory.
After submiting the info, the device will switch to STA mode and connect to the provisioned AP. The webpage will redirect you
to http://<static_ip>. The page will obviously only load if the mobile/laptop is reconnected to the WAP that the esp32 device is connecting.
The webpage on the esp should now show system information.



# Configure pins to monitor and control
curl http://192.168.1.46/pins -d '{"outputs":[13,14], "inputs":[22,23]}'

# OTA upgrade
./ota_flash <ip of device>

# API
 Delete wifi credentials and restart, dropping you in provision mode: curl -vvv http://192.168.1.45/restart -X DELETE
 Restart: curl -vvv http://192.168.1.45/restart -X POST
 Provision: curl http://192.168.4.1/data -d '{"wifi":{"ssid":"<SSID>", "password":"<PASSWORD>"}, "config":{"ip":"192.168.1.46", "gw":"192.168.1.1", "dhcp":true. "active":false}}'
