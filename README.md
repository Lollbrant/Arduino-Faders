# Arduino-Faders
A device with faders to control audio on a PC

## Move the files to their correct locations
linux/HyperControl.py -> /opt/HyperControl/linux/HyperControl.py\
linux/hypercontrol.service -> ~/.config/systemd/user/hypercontrol.service


## Load and start the new service
```
systemctl --user daemon-reexec
systemctl --user daemon-reload
systemctl --user enable --now hypercontrol.service
```
