<template>
  <div class="register-container">
    <h1>Регистрация</h1>
    <div v-if="error" class="error">{{ error }}</div>
    <form @submit.prevent="handleRegister">
      <div class="form-group">
        <label for="email">Email</label>
        <input
          type="email"
          id="email"
          v-model="email"
          placeholder="Введите email"
          required
        />
      </div>
      <div class="form-group">
        <label for="password">Пароль</label>
        <input
          type="password"
          id="password"
          v-model="password"
          placeholder="Введите пароль"
          required
          minlength="6"
        />
      </div>
      <div class="form-group">
        <label for="confirmPassword">Подтверждение пароля</label>
        <input
          type="password"
          id="confirmPassword"
          v-model="confirmPassword"
          placeholder="Подтвердите пароль"
          required
        />
      </div>
      <button
        type="submit"
        :disabled="loading"
      >
        {{ loading ? 'Регистрация...' : 'Зарегистрироваться' }}
      </button>
      <div class="login-link">
        <p>Уже есть аккаунт? <router-link to="/login">Войти</router-link></p>
      </div>
    </form>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const email = ref('')
const password = ref('')
const confirmPassword = ref('')
const error = ref('')
const loading = ref(false)
const router = useRouter()
const authStore = useAuthStore()

async function handleRegister() {
  // Валидация
  if (!email.value || !password.value || !confirmPassword.value) {
    error.value = 'Пожалуйста, заполните все поля'
    return
  }

  if (password.value !== confirmPassword.value) {
    error.value = 'Пароли не совпадают'
    return
  }

  if (password.value.length < 6) {
    error.value = 'Пароль должен быть не менее 6 символов'
    return
  }

  loading.value = true
  error.value = ''

  try {
    const result = await authStore.register(email.value, password.value)
    if (result.success) {
      router.push('/login')
    } else {
      error.value = result.error || 'Ошибка регистрации'
    }
  } catch (e) {
    error.value = 'Ошибка регистрации'
    console.error(e)
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.register-container {
  max-width: 400px;
  margin: 0 auto;
  padding: 20px;
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

button {
  width: 100%;
  padding: 10px;
  background-color: #4CAF50;
  color: white;
  border: none;
  border-radius: 4px;
  cursor: pointer;
}

button:hover {
  background-color: #45a049;
}

button:disabled {
  background-color: #cccccc;
  cursor: not-allowed;
}

.error {
  color: red;
  margin-bottom: 10px;
}

.login-link {
  margin-top: 15px;
  text-align: center;
}
</style>