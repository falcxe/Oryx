<template>
  <div class="dashboard-container">
    <header class="dashboard-header">
      <h1>Личный кабинет</h1>
      <button @click="logout" class="logout-btn">Выйти</button>
    </header>

    <div class="user-info" v-if="user">
      <h2>Профиль пользователя</h2>
      <p><strong>Email:</strong> {{ user.email }}</p>
    </div>

    <!-- Секция устройств -->
    <div class="devices-section">
      <h2>Мои устройства</h2>
      <div v-if="devicesLoading">Загрузка устройств...</div>
      <div v-else-if="devicesError" class="error">{{ devicesError }}</div>
      <div v-else-if="devices.length === 0" class="empty-state">
        <p>У вас пока нет устройств</p>
        <button @click="showAddDeviceForm = true" class="primary-btn">Добавить устройство</button>
      </div>
      <div v-else class="devices-list">
        <div
          v-for="device in devices"
          :key="device.id"
          class="device-card"
          :class="{ 'active': activeDevice && activeDevice.id === device.id }"
          @click="selectDevice(device)"
        >
          <h3>{{ device.name }}</h3>
          <p>Создано: {{ formatDate(device.created_at) }}</p>
        </div>
        <button @click="showAddDeviceForm = true" class="add-btn">+ Добавить устройство</button>
      </div>

      <!-- Форма добавления устройства -->
      <div v-if="showAddDeviceForm" class="modal">
        <div class="modal-content">
          <h3>Добавить устройство</h3>
          <div v-if="deviceFormError" class="error">{{ deviceFormError }}</div>
          <input
            type="text"
            v-model="newDeviceName"
            placeholder="Название устройства"
            required
          />
          <div class="modal-actions">
            <button @click="showAddDeviceForm = false" class="secondary-btn">Отмена</button>
            <button @click="addDevice" class="primary-btn" :disabled="deviceFormLoading">
              {{ deviceFormLoading ? 'Добавление...' : 'Добавить' }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Секция поездок -->
    <div class="trips-section">
      <h2>История поездок</h2>
      <div v-if="tripsLoading">Загрузка поездок...</div>
      <div v-else-if="tripsError" class="error">{{ tripsError }}</div>
      <div v-else-if="trips.length === 0" class="empty-state">
        <p>У вас пока нет поездок</p>
        <button @click="showAddTripForm = true" :disabled="!activeDevice" class="primary-btn">
          {{ activeDevice ? 'Добавить поездку' : 'Сначала выберите устройство' }}
        </button>
      </div>
      <div v-else>
        <table class="trips-table">
          <thead>
            <tr>
              <th>Дата</th>
              <th>Устройство</th>
              <th>Расстояние (км)</th>
              <th>Ср. скорость (км/ч)</th>
              <th>Длительность</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="trip in trips" :key="trip.id">
              <td>{{ formatDate(trip.start_time) }}</td>
              <td>{{ getDeviceName(trip.device_id) }}</td>
              <td>{{ trip.distance.toFixed(2) }}</td>
              <td>{{ trip.avg_speed.toFixed(2) }}</td>
              <td>{{ calculateDuration(trip.start_time, trip.end_time) }}</td>
            </tr>
          </tbody>
        </table>
        <button @click="showAddTripForm = true" :disabled="!activeDevice" class="add-btn">
          + Добавить поездку
        </button>
      </div>

      <!-- Форма добавления поездки -->
      <div v-if="showAddTripForm && activeDevice" class="modal">
        <div class="modal-content">
          <h3>Добавить поездку</h3>
          <div v-if="tripFormError" class="error">{{ tripFormError }}</div>

          <div class="form-group">
            <label for="tripDistance">Расстояние (км)</label>
            <input
              type="number"
              id="tripDistance"
              v-model="newTripData.distance"
              step="0.01"
              min="0"
              required
            />
          </div>

          <div class="form-group">
            <label for="tripSpeed">Средняя скорость (км/ч)</label>
            <input
              type="number"
              id="tripSpeed"
              v-model="newTripData.avg_speed"
              step="0.01"
              min="0"
              required
            />
          </div>

          <div class="form-group">
            <label for="tripStartTime">Время начала</label>
            <input
              type="datetime-local"
              id="tripStartTime"
              v-model="newTripData.start_time"
              required
            />
          </div>

          <div class="form-group">
            <label for="tripEndTime">Время окончания</label>
            <input
              type="datetime-local"
              id="tripEndTime"
              v-model="newTripData.end_time"
              required
            />
          </div>

          <div class="modal-actions">
            <button @click="showAddTripForm = false" class="secondary-btn">Отмена</button>
            <button @click="addTrip" class="primary-btn" :disabled="tripFormLoading">
              {{ tripFormLoading ? 'Добавление...' : 'Добавить' }}
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'
import { useTripsStore } from '../stores/trips'
import { useDevicesStore } from '../stores/devices'

const router = useRouter()
const authStore = useAuthStore()
const tripsStore = useTripsStore()
const devicesStore = useDevicesStore()

// Состояния из сторов
const user = computed(() => authStore.user)
const trips = computed(() => tripsStore.trips)
const tripsLoading = computed(() => tripsStore.loading)
const tripsError = computed(() => tripsStore.error)
const devices = computed(() => devicesStore.devices)
const devicesLoading = computed(() => devicesStore.loading)
const devicesError = computed(() => devicesStore.error)
const activeDevice = computed(() => devicesStore.activeDevice)

// Локальные состояния
const showAddDeviceForm = ref(false)
const showAddTripForm = ref(false)
const newDeviceName = ref('')
const deviceFormLoading = ref(false)
const deviceFormError = ref('')
const tripFormLoading = ref(false)
const tripFormError = ref('')
const newTripData = ref({
  device_id: null,
  start_time: '',
  end_time: '',
  distance: 0,
  avg_speed: 0,
})

// Загрузка данных при монтировании
onMounted(async () => {
  if (!authStore.isAuthenticated) {
    router.push('/login')
    return
  }

  await Promise.all([
    devicesStore.fetchDevices(),
    tripsStore.fetchTrips()
  ])
})

// Действия
function logout() {
  authStore.logout()
  router.push('/login')
}

function selectDevice(device) {
  devicesStore.setActiveDevice(device)
  // Обновляем данные новой поездки
  newTripData.value.device_id = device.id
}

async function addDevice() {
  if (!newDeviceName.value) {
    deviceFormError.value = 'Введите название устройства'
    return
  }

  deviceFormLoading.value = true
  deviceFormError.value = ''

  try {
    const result = await devicesStore.createDevice(newDeviceName.value)
    if (result.success) {
      showAddDeviceForm.value = false
      newDeviceName.value = ''
    } else {
      deviceFormError.value = result.error || 'Ошибка создания устройства'
    }
  } catch (e) {
    deviceFormError.value = 'Произошла ошибка'
  } finally {
    deviceFormLoading.value = false
  }
}

async function addTrip() {
  // Валидация
  if (!newTripData.value.start_time || !newTripData.value.end_time ||
      newTripData.value.distance <= 0 || newTripData.value.avg_speed <= 0) {
    tripFormError.value = 'Заполните все поля корректно'
    return
  }

  // Установка ID устройства
  newTripData.value.device_id = activeDevice.value.id

  tripFormLoading.value = true
  tripFormError.value = ''

  try {
    const result = await tripsStore.createTrip({
      device_id: newTripData.value.device_id,
      start_time: new Date(newTripData.value.start_time).toISOString(),
      end_time: new Date(newTripData.value.end_time).toISOString(),
      distance: parseFloat(newTripData.value.distance),
      avg_speed: parseFloat(newTripData.value.avg_speed)
    })

    if (result.success) {
      showAddTripForm.value = false
      // Сброс формы
      newTripData.value = {
        device_id: activeDevice.value.id,
        start_time: '',
        end_time: '',
        distance: 0,
        avg_speed: 0,
      }
    } else {
      tripFormError.value = result.error || 'Ошибка создания поездки'
    }
  } catch (e) {
    tripFormError.value = 'Произошла ошибка'
    console.error(e)
  } finally {
    tripFormLoading.value = false
  }
}

// Вспомогательные функции
function formatDate(dateString) {
  if (!dateString) return ''
  const date = new Date(dateString)
  return date.toLocaleString()
}

function calculateDuration(start, end) {
  if (!start || !end) return ''
  const startDate = new Date(start)
  const endDate = new Date(end)
  const diffMs = endDate - startDate
  const diffHrs = Math.floor(diffMs / 3600000)
  const diffMins = Math.round((diffMs % 3600000) / 60000)
  return `${diffHrs}ч ${diffMins}мин`
}

function getDeviceName(deviceId) {
  const device = devices.value.find(d => d.id === deviceId)
  return device ? device.name : 'Неизвестное устройство'
}
</script>

<style scoped>
.dashboard-container {
  max-width: 1200px;
  margin: 0 auto;
  padding: 20px;
}

.dashboard-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 30px;
}

.user-info {
  background-color: #f5f5f5;
  border-radius: 8px;
  padding: 15px;
  margin-bottom: 30px;
}

.devices-section, .trips-section {
  margin-bottom: 30px;
}

.devices-list {
  display: flex;
  flex-wrap: wrap;
  gap: 15px;
  margin-top: 15px;
}

.device-card {
  background-color: #f5f5f5;
  border-radius: 8px;
  padding: 15px;
  width: 200px;
  cursor: pointer;
  transition: all 0.2s;
  border: 2px solid transparent;
}

.device-card:hover {
  background-color: #e9e9e9;
}

.device-card.active {
  border-color: #4CAF50;
  background-color: #e8f5e9;
}

.trips-table {
  width: 100%;
  border-collapse: collapse;
  margin-top: 15px;
}

.trips-table th, .trips-table td {
  border: 1px solid #ddd;
  padding: 12px;
  text-align: left;
}

.trips-table th {
  background-color: #f2f2f2;
  font-weight: bold;
}

.trips-table tr:nth-child(even) {
  background-color: #f8f8f8;
}

.primary-btn {
  background-color: #4CAF50;
  color: white;
  border: none;
  padding: 10px 15px;
  border-radius: 4px;
  cursor: pointer;
}

.primary-btn:hover {
  background-color: #45a049;
}

.primary-btn:disabled {
  background-color: #cccccc;
  cursor: not-allowed;
}

.secondary-btn {
  background-color: #f5f5f5;
  color: #333;
  border: 1px solid #ddd;
  padding: 10px 15px;
  border-radius: 4px;
  cursor: pointer;
}

.secondary-btn:hover {
  background-color: #e8e8e8;
}

.add-btn {
  background-color: #e8f5e9;
  color: #4CAF50;
  border: 1px dashed #4CAF50;
  padding: 10px 15px;
  border-radius: 4px;
  cursor: pointer;
  display: block;
  margin-top: 15px;
}

.add-btn:hover {
  background-color: #d5ecd6;
}

.logout-btn {
  background-color: #f44336;
  color: white;
  border: none;
  padding: 8px 15px;
  border-radius: 4px;
  cursor: pointer;
}

.logout-btn:hover {
  background-color: #d32f2f;
}

.empty-state {
  text-align: center;
  padding: 30px;
  background-color: #f9f9f9;
  border-radius: 8px;
}

.error {
  color: red;
  margin-bottom: 15px;
}

.modal {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
  display: flex;
  justify-content: center;
  align-items: center;
}

.modal-content {
  background-color: white;
  padding: 25px;
  border-radius: 8px;
  width: 400px;
  max-width: 90%;
}

.modal-actions {
  display: flex;
  justify-content: space-between;
  margin-top: 20px;
}

.form-group {
  margin-bottom: 15px;
}

label {
  display: block;
  margin-bottom: 5px;
  font-weight: bold;
}

input {
  width: 100%;
  padding: 8px;
  border: 1px solid #ddd;
  border-radius: 4px;
}
</style>