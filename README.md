*This project is provided as-is. I built it for my own use and share it in case others find it useful. I do not offer any support.*
# Arduino-Faders
A device with physical faders to control audio levels on a Linux PC using PipeWire and WirePlumber


## Install the Python Service
Copy the following files to their expected locations:

- `linux/HyperControl.py` → `/opt/HyperControl/linux/HyperControl.py`
- `linux/hypercontrol.service` → `~/.config/systemd/user/hypercontrol.service`

## Configure Which Applications Are Controlled

Open the `HyperControl.py` script and scroll to the top. You'll find a dictionary called `FADER_CONTROL`:

```python
FADER_CONTROL = {
    1: "alsa_output",
    2: "upmix",
    3: "Firefox",
    4: "Chromium",
    5: "Brave"
}
```
Each key is a fader ID (as defined in the Arduino code), and the value is the name of the application or audio sink you want that fader to control.

### Customization
Run this command to get a full list of currently available PipeWire nodes:
```bash
pw-dump | jq '.[] | .info.props | {name: .["node.name"], app: .["application.name"]}'
```
You can then copy the relevant `node.name` or `application.name` into the `FADER_CONTROL` list.\
Note that it will match on any application that shares the same name.

## Load and Start the Service
This service reads serial data from the Arduino and sets audio node volumes.
```bash
systemctl --user daemon-reexec
systemctl --user daemon-reload
systemctl --user enable --now hypercontrol.service
```
