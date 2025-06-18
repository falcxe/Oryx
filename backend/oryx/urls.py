from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/v1/devices/', include('devices.urls')),
    #path('api/v1/ride/', include('rides.urls')),
    path('api/v1/user/', include('users.urls')),
]
