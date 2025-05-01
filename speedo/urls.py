from django.urls import path
from . import views

urlpatterns = [
    path('', views.dashboard, name='dashboard'),
    path('routes/', views.routes, name='routes'),
    path('history/', views.routes, name='history'),
    path('settings/', views.settings, name='settings'),
    path('api/receive/', views.receive_data, name='receive_data'),
    path('api/latest/', views.get_latest_data, name='get_latest_data'),
]