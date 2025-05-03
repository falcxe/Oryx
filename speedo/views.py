from django.shortcuts import render, redirect, get_object_or_404
from django.http import JsonResponse, HttpResponse
from django.views.decorators.csrf import csrf_exempt
from django.contrib.auth.decorators import login_required
import json
from .models import SpeedometerData, DeviceRegistration
from django.utils import timezone
from datetime import datetime
from django.views.decorators.http import require_POST
import copy
import uuid
from django.contrib import messages

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
    # Аутентификация устройства
    device = authenticate_device(request)
    
    if not device:
        return JsonResponse({
            'success': False,
            'message': 'Неавторизованное устройство'
        }, status=401)
    
    try:
        data = json.loads(request.body)

        # Добавляем device_id из аутентифицированного устройства
        data['device_id'] = device.device_id
        
        # Обновляем временное хранилище
        for key, value in data.items():
            if key in latest_data:
                if key == 'acceleration' and isinstance(value, dict):
                    for axis, axis_value in value.items():
                        if axis in latest_data['acceleration']:
                            latest_data['acceleration'][axis] = axis_value
                else:
                    latest_data[key] = value

        # Текущая временная метка
        latest_data['timestamp'] = datetime.now().isoformat()
        
        # Сохраняем данные в БД (опционально)
        # speedometer_data = SpeedometerData(...)
        # speedometer_data.save()

        return JsonResponse({
            'success': True,
            'message': 'Данные успешно получены',
            'device_name': device.device_name
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

@csrf_exempt
def device_settings(request):
    """API эндпоинт для получения и обновления настроек устройства"""
    if request.method == 'GET':
        # Отправляем текущие настройки для устройства
        return JsonResponse({
            'success': True,
            'gps_sound_enabled': True,
            'sync_interval': 10,  # в секундах
            'display_brightness': 255,
            'led_brightness': 1
        })
    elif request.method == 'POST':
        # Обрабатываем запрос на обновление настроек
        try:
            data = json.loads(request.body)
            # Здесь можно добавить код для сохранения полученных настроек
            # в базу данных или другое хранилище
            
            return JsonResponse({
                'success': True,
                'message': 'Настройки обновлены'
            })
        except Exception as e:
            return JsonResponse({
                'success': False,
                'message': f'Ошибка при обработке данных: {str(e)}'
            }, status=400)

# Функции для работы с устройствами

@login_required
def device_list(request):
    """Страница со списком устройств пользователя"""
    devices = DeviceRegistration.objects.filter(user=request.user)
    return render(request, 'speedo/device_list.html', {'devices': devices})

@login_required
def add_device(request):
    """Страница добавления нового устройства"""
    if request.method == 'POST':
        device_id = request.POST.get('device_id')
        device_name = request.POST.get('device_name', '')
        
        # Проверка существования устройства с таким ID
        if DeviceRegistration.objects.filter(device_id=device_id).exists():
            messages.error(request, f'Устройство с ID {device_id} уже зарегистрировано')
            return redirect('device_list')
        
        # Создание новой записи устройства
        device = DeviceRegistration(
            user=request.user,
            device_id=device_id,
            device_name=device_name
        )
        device.save()
        
        messages.success(request, f'Устройство {device_name or device_id} успешно добавлено')
        return redirect('device_list')
    
    return render(request, 'speedo/add_device.html')

@login_required
def device_details(request, device_id):
    """Страница просмотра деталей устройства"""
    device = get_object_or_404(DeviceRegistration, id=device_id, user=request.user)
    
    if request.method == 'POST':
        action = request.POST.get('action')
        
        if action == 'rename':
            device.device_name = request.POST.get('device_name', '')
            device.save()
            messages.success(request, f'Имя устройства изменено на {device.device_name}')
        
        elif action == 'regenerate_token':
            device.token = uuid.uuid4()
            device.save()
            messages.success(request, 'Токен устройства обновлен')
        
        elif action == 'toggle_active':
            device.is_active = not device.is_active
            device.save()
            status = 'активировано' if device.is_active else 'деактивировано'
            messages.success(request, f'Устройство {status}')
        
        elif action == 'delete':
            device.delete()
            messages.success(request, 'Устройство удалено')
            return redirect('device_list')
        
        return redirect('device_details', device_id=device.id)
    
    # Получаем последние данные с устройства
    last_data = SpeedometerData.objects.filter(device_id=device.device_id).order_by('-timestamp').first()
    
    return render(request, 'speedo/device_details.html', {
        'device': device,
        'last_data': last_data
    })

@csrf_exempt
def device_pair(request):
    """API для начальной привязки устройства (установка кода сопряжения)"""
    if request.method == 'POST':
        try:
            data = json.loads(request.body)
            device_id = data.get('device_id')
            pairing_code = data.get('pairing_code')
            
            if not device_id or not pairing_code:
                return JsonResponse({
                    'success': False,
                    'message': 'Не указан device_id или pairing_code'
                }, status=400)
            
            # Генерируем временный код сопряжения и сохраняем его
            # Здесь нужно реализовать хранение временных кодов сопряжения
            # Например, в Redis или в отдельной модели
            
            # В этом примере мы просто возвращаем успех
            return JsonResponse({
                'success': True,
                'message': 'Устройство готово к сопряжению',
                'pairing_code': pairing_code,
                'expires_in': 300  # Код действителен 5 минут
            })
            
        except Exception as e:
            return JsonResponse({
                'success': False,
                'message': f'Ошибка: {str(e)}'
            }, status=400)
    
    return JsonResponse({
        'success': False,
        'message': 'Метод не поддерживается'
    }, status=405)

@login_required
def confirm_device_pairing(request):
    """Страница для ввода кода сопряжения и привязки устройства к аккаунту"""
    if request.method == 'POST':
        pairing_code = request.POST.get('pairing_code')
        device_name = request.POST.get('device_name', '')
        
        # Здесь должна быть проверка кода сопряжения
        # и связывание устройства с пользователем
        
        # Пример создания записи устройства (в реальности ID должно приходить от устройства)
        device_id = f"ESP32-{uuid.uuid4().hex[:8]}"
        
        device = DeviceRegistration(
            user=request.user,
            device_id=device_id,
            device_name=device_name
        )
        device.save()
        
        messages.success(request, f'Устройство {device_name or device_id} успешно сопряжено')
        return redirect('device_list')
    
    return render(request, 'speedo/confirm_pairing.html')

# Аутентификация устройства и проверка токена
def authenticate_device(request):
    """Проверяет токен в заголовке запроса и возвращает объект устройства"""
    auth_header = request.headers.get('Authorization', '')
    
    if not auth_header.startswith('Bearer '):
        return None
    
    token = auth_header.split(' ')[1]
    
    try:
        device = DeviceRegistration.objects.get(token=token, is_active=True)
        # Обновляем время последнего подключения
        device.last_connected = timezone.now()
        device.save(update_fields=['last_connected'])
        return device
    except DeviceRegistration.DoesNotExist:
        return None

@csrf_exempt
def api_auth(request):
    """API для аутентификации устройства и получения токена"""
    if request.method == 'POST':
        try:
            data = json.loads(request.body)
            device_id = data.get('device_id')
            
            if not device_id:
                return JsonResponse({
                    'success': False,
                    'message': 'Не указан device_id'
                }, status=400)
            
            try:
                device = DeviceRegistration.objects.get(device_id=device_id, is_active=True)
                return JsonResponse({
                    'success': True,
                    'token': str(device.token),
                    'device_name': device.device_name
                })
            except DeviceRegistration.DoesNotExist:
                return JsonResponse({
                    'success': False,
                    'message': 'Устройство не найдено или не активно'
                }, status=404)
            
        except Exception as e:
            return JsonResponse({
                'success': False,
                'message': f'Ошибка: {str(e)}'
            }, status=400)
    
    return JsonResponse({
        'success': False,
        'message': 'Метод не поддерживается'
    }, status=405)
