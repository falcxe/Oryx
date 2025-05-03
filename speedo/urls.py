from django.urls import path
from . import views

urlpatterns = [
    path('', views.dashboard, name='dashboard'),
    path('routes/', views.routes, name='routes'),
    path('history/', views.routes, name='history'),
    path('settings/', views.settings, name='settings'),
    path('api/receive/', views.receive_data, name='receive_data'),
    path('api/latest/', views.get_latest_data, name='get_latest_data'),
    path('api/settings/', views.device_settings, name='device_settings'),
    
    # URLs для управления устройствами
    path('devices/', views.device_list, name='device_list'),
    path('devices/add/', views.add_device, name='add_device'),
    path('devices/<int:device_id>/', views.device_details, name='device_details'),
    path('devices/pair/', views.confirm_device_pairing, name='confirm_device_pairing'),
    
    # APIs для устройств
    path('api/auth/', views.api_auth, name='api_auth'),
    path('api/pair/', views.device_pair, name='device_pair'),
]