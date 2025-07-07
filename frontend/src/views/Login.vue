<template>
  <div class="login-container">
    <h1>Вход</h1>
    <div v-if="error" class="error">{{ error }}</div>
    <form @submit.prevent="handleLogin">
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
        />
      </div>
      <button
        type="submit"
        :disabled="loading"
      >
        {{ loading ? 'Входим...' : 'Войти' }}
      </button>
      <div class="register-link">
        <p>Ещё нет аккаунта? <router-link to="/register">Зарегистрироваться</router-link></p>
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
const error = ref('')
const loading = ref(false)
const router = useRouter()
const authStore = useAuthStore()

async function handleLogin() {
  if (!email.value || !password.value) {
    error.value = 'Пожалуйста, заполните все поля'
    return
  }

  loading.value = true
  error.value = ''

  try {
    const result = await authStore.login(email.value, password.value)
    if (result.success) {
      router.push('/dashboard')
    } else {
      error.value = result.error || 'Ошибка входа'
    }
  } catch (e) {
    error.value = 'Ошибка авторизации'
    console.error(e)
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-container {
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

.register-link {
  margin-top: 15px;
  text-align: center;
}
</style>