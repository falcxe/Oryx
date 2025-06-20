<template>
  <div>
    <h1>Вход</h1>
    <form>
      <input type="text" placeholder="Логин" />
      <input type="password" placeholder="Пароль" />
      <button type="submit">Войти</button>
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