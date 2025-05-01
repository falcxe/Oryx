from django.shortcuts import render
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
import json
from .models import SpeedometerData
from django.utils import timezone
from datetime import datetime
from django.views.decorators.http import require_POST
import copy

# Create your views here.

# Временное хранилище для последних полученных данных
latest_data = {
    'speed': 0,
    'max_speed': 0,
    'avg_speed': 0,
    'distance': 0,
    'calories': 0,
    'latitude': 55.7558,
    'longitude': 37.6173,
    'hall_sensor': False,
    'magnet_sensor': False,
    'light_sensor': False,
    'switch1': False,
    'switch2': False,
    'battery': 100,
    'trip_time': 0,
    'acceleration': {
        'x': 0.0,
        'y': 0.0,
        'z': 0.0
    },
    'device_id': 'ESP32-001',
    'timestamp': datetime.now().isoformat()
}

@csrf_exempt
@require_POST
def receive_data(request):
    """API эндпоинт для приема данных от ESP32"""
    try:
        data = json.loads(request.body)

        # Обновляем временное хранилище полученными данными
        for key, value in data.items():
            if key in latest_data:
                # Обрабатываем акселерометр, который является вложенным объектом
                if key == 'acceleration' and isinstance(value, dict):
                    for axis, axis_value in value.items():
                        if axis in latest_data['acceleration']:
                            latest_data['acceleration'][axis] = axis_value
                else:
                    latest_data[key] = value

        # Добавляем текущую временную метку
        latest_data['timestamp'] = datetime.now().isoformat()

        return JsonResponse({
            'success': True,
            'message': 'Данные успешно получены'
        })
    except Exception as e:
        return JsonResponse({
            'success': False,
            'message': f'Ошибка при обработке данных: {str(e)}'
        }, status=400)

def get_latest_data(request):
    """API эндпоинт для получения последних данных"""
    try:
        # Создаем копию данных, чтобы не изменять оригинал
        safe_data = {}

        # Копируем только сериализуемые данные
        for key, value in latest_data.items():
            if not callable(value):
                if isinstance(value, dict):
                    # Рекурсивное копирование вложенных словарей
                    safe_data[key] = {}
                    for sub_key, sub_value in value.items():
                        if not callable(sub_value):
                            safe_data[key][sub_key] = sub_value
                else:
                    safe_data[key] = value

        return JsonResponse({
            'success': True,
            'data': safe_data
        })
    except Exception as e:
        return JsonResponse({
            'success': False,
            'message': f'Ошибка при получении данных: {str(e)}'
        }, status=500)

def dashboard(request):
    last_data = SpeedometerData.objects.last()
    return render(request, 'speedo/dashboard.html', {'data': last_data})

def routes(request):
    return render(request, 'speedo/routes.html')

def history(request):
    return render(request, 'speedo/history.html')

def settings(request):
    return render(request, 'speedo/settings.html')

def sensors(request):
    """Страница с графиками показаний датчиков"""
    return render(request, 'speedo/sensors.html')

def latest_data(request):
    last = SpeedometerData.objects.last()
    if last:
        return JsonResponse({
            'speed': last.speed,
            'latitude': last.latitude,
            'longitude': last.longitude,
            'hall_sensor': last.hall_sensor,
            'rgb_led': last.rgb_led,
            'buzzer': last.buzzer,
            'switch1': last.switch1,
            'switch2': last.switch2,
            'timestamp': last.timestamp.strftime('%d.%m.%Y %H:%M:%S')
        })
    return JsonResponse({'error': 'no data'}, status=404)
