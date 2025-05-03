# Django проект для Railway

Этот репозиторий содержит Django-проект, настроенный для деплоя на платформу Railway.

## Требования

- Python 3.10+
- Django 5.1.4
- PostgreSQL (предоставляется Railway)

## Локальная разработка

1. Клонируйте репозиторий:
```
git clone <url-репозитория>
cd <имя-директории>
```

2. Создайте виртуальное окружение и активируйте его:
```
python -m venv venv
source venv/bin/activate  # На Windows: venv\Scripts\activate
```

3. Установите зависимости:
```
pip install -r requirements.txt
```

4. Скопируйте файл .env-example в .env и настройте переменные окружения:
```
cp .env-example .env
```

5. Выполните миграции:
```
python manage.py migrate
```

6. Запустите сервер разработки:
```
python manage.py runserver
```

## Деплой на Railway

### Метод 1: Деплой через Railway CLI

1. Установите Railway CLI:
```
npm i -g @railway/cli
```

2. Залогиньтесь в Railway:
```
railway login
```

3. Инициализируйте проект Railway:
```
railway init
```

4. Подключите проект к Railway:
```
railway link
```

5. Добавьте сервис PostgreSQL:
```
railway add
```
Выберите PostgreSQL из списка доступных сервисов.

6. Деплой проекта:
```
railway up
```

### Метод 2: Деплой через GitHub

1. Загрузите свой проект на GitHub.

2. Зайдите на [Railway](https://railway.app/) и создайте аккаунт (если еще нет).

3. Нажмите "New Project" и выберите "Deploy from GitHub".

4. Выберите ваш GitHub репозиторий.

5. Добавьте сервис базы данных PostgreSQL, нажав "New Service" и выбрав PostgreSQL.

6. Настройте следующие переменные окружения в разделе "Variables":
   - `SECRET_KEY`: ваш секретный ключ
   - `DEBUG`: False
   - `ALLOWED_HOSTS`: .railway.app
   - `CSRF_TRUSTED_ORIGINS`: https://*.railway.app

7. Railway автоматически распознает команды в Procfile и запустит ваш проект.

## После деплоя

1. Выполните миграции через Railway CLI:
```
railway run python manage.py migrate
```

2. Создайте суперпользователя:
```
railway run python manage.py createsuperuser
```

## Структура проекта

- `oryx_smart_speedometer/` - основной модуль проекта
- `speedo/` - приложение велоспидометра
- `Procfile` - инструкции для запуска на Railway
- `requirements.txt` - зависимости Python
- `runtime.txt` - версия Python 