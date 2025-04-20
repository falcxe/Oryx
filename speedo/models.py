from django.db import models


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