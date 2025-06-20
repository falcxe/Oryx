<template>
  <div>
    <h1>Регистрация</h1>
    <form>
      <input type="text" placeholder="Логин" />
      <input type="email" placeholder="Email" />
      <input type="password" placeholder="Пароль" />
      <button type="submit">Зарегистрироваться</button>
    </form>
  </div>
</template>

<script setup>
</script>

<style scoped>
form {
  display: flex;
  flex-direction: column;
  max-width: 300px;
  margin: 0 auto;
}
input, button {
  margin-bottom: 12px;
  padding: 8px;
}
</style>