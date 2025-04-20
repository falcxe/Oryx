from django.urls import path
from . import views

urlpatterns = [
    path('api/receive/', views.receive_data, name='receive_data'),
    path('', views.dashboard, name='dashboard'),
    path('routes/', views.routes, name='routes'),
    path('history/', views.history, name='history'),
    path('settings/', views.settings, name='settings'),
]