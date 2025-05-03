from django.db import models
from django.contrib.auth.models import User
import uuid
from django.utils import timezone


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


class PairingCode(models.Model):
    """Модель для хранения временных кодов сопряжения устройств"""
    code = models.CharField(max_length=6, unique=True)
    device_id = models.CharField(max_length=50)
    created_at = models.DateTimeField(auto_now_add=True)
    expires_at = models.DateTimeField()
    is_paired = models.BooleanField(default=False)
    paired_by = models.ForeignKey(User, on_delete=models.SET_NULL, null=True, blank=True)
    
    def __str__(self):
        return f"Код {self.code} для {self.device_id}"
    
    def is_valid(self):
        """Проверяет, действителен ли код сопряжения"""
        return not self.is_paired and timezone.now() < self.expires_at
    
    @classmethod
    def create_code(cls, device_id, code, expires_in_minutes=5):
        """Создает новый код сопряжения с указанным сроком действия"""
        expires_at = timezone.now() + timezone.timedelta(minutes=expires_in_minutes)
        
        # Удаляем старые коды для этого устройства
        cls.objects.filter(device_id=device_id).delete()
        
        # Создаем новый код
        return cls.objects.create(
            code=code,
            device_id=device_id,
            expires_at=expires_at
        )