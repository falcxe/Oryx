from django.shortcuts import render
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
import json
from .models import SpeedometerData

# Create your views here.

@csrf_exempt
def receive_data(request):
    if request.method == 'POST':
        data = json.loads(request.body)
        SpeedometerData.objects.create(
            speed=data['speed'],
            latitude=data['latitude'],
            longitude=data['longitude'],
            hall_sensor=data['hall_sensor'],
            rgb_led=data['rgb_led'],
            buzzer=data['buzzer'],
            switch1=data['switch1'],
            switch2=data['switch2'],
        )
        return JsonResponse({'status': 'ok'})
    return JsonResponse({'error': 'Invalid request'}, status=400)

def dashboard(request):
    last_data = SpeedometerData.objects.last()
    return render(request, 'speedo/dashboard.html', {'data': last_data})

def routes(request):
    return render(request, 'speedo/routes.html')

def history(request):
    return render(request, 'speedo/history.html')

def settings(request):
    return render(request, 'speedo/settings.html')
