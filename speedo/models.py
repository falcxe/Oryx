from django.db import models
from django.contrib.auth.models import User
import uuid


class SpeedometerData(models.Model):
    timestamp = models.DateTimeField(auto_now_add=True)
    speed = models.FloatField()
    latitude = models.FloatField()
    longitude = models.FloatField()
    hall_sensor = models.BooleanField()
    rgb_led = models.CharField(max_length=20)
    buzzer = models.BooleanField()
    switch1 = models.BooleanField()
    switch2 = models.BooleanField()

    def __str__(self):
        return f"{self.timestamp}: {self.speed} км/ч"


class DeviceRegistration(models.Model):
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='devices')
    device_id = models.CharField(max_length=50, unique=True)
    device_name = models.CharField(max_length=100, blank=True)
    token = models.UUIDField(default=uuid.uuid4, editable=False, unique=True)
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)
    last_connected = models.DateTimeField(null=True, blank=True)
    
    def __str__(self):
        return f"{self.device_name or self.device_id} ({self.user.username})"