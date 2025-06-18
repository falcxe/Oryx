# Oryx

[![Python](https://img.shields.io/badge/python-3.11%2B-blue.svg)](https://www.python.org/downloads/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Built with uv](https://img.shields.io/badge/built%20with-uv-6e40c9?logo=python&logoColor=white)](https://docs.astral.sh/uv/)

> ⚡️ Smart bicycle speedometer

## Overview

This repository contains:
- **backend/** — Django backend
- **esp32/** — ESP32 firmware (Arduino)

## Quick Start

### Clone the repository
```bash
 git clone https://github.com/falcxe/Oryx.git
 cd Oryx
```

### Install dependencies (using [uv](https://docs.astral.sh/uv/))
```bash
uv sync
```

### Apply migrations and run the server
```bash
cd backend
python manage.py makemigrations
python manage.py migrate
python manage.py runserver
```

## ESP32
The `esp32/` folder contains Arduino code for the ESP32 microcontroller. Flash it using the Arduino IDE or PlatformIO.

## Documentation
- [Django documentation](https://docs.djangoproject.com/)
- [ESP32 Arduino](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [uv documentation](https://docs.astral.sh/uv/)
