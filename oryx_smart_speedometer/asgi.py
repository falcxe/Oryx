"""
ASGI config for oryx_smart_speedometer project.

It exposes the ASGI callable as a module-level variable named ``application``.

For more information on this file, see
https://docs.djangoproject.com/en/5.1/howto/deployment/asgi/
"""

import os
import sys

# Добавляем путь к проекту для Railway
path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if path not in sys.path:
    sys.path.append(path)

from django.core.asgi import get_asgi_application

# Настройка переменной окружения DJANGO_SETTINGS_MODULE
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'oryx_smart_speedometer.settings')

# Получаем приложение ASGI
application = get_asgi_application()
