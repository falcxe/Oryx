#!/usr/bin/env python
"""
Скрипт для миграции данных из SQLite в PostgreSQL.
Используйте его перед деплоем проекта на Railway.

Запуск:
python scripts/migrate_to_postgres.py

Требуется установить:
pip install psycopg2-binary dj-database-url
"""

import os
import sys
import json
import sqlite3
import psycopg2
from urllib.parse import urlparse

# Добавляем корневую директорию проекта в sys.path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Загружаем переменные окружения для PostgreSQL
postgres_url = os.environ.get('DATABASE_URL')

if not postgres_url:
    print("Ошибка: Переменная окружения DATABASE_URL не найдена.")
    print("Пример: DATABASE_URL=postgresql://user:password@host:port/dbname")
    sys.exit(1)

# Парсим URL для подключения к PostgreSQL
url = urlparse(postgres_url)
dbname = url.path[1:]  # Удаляем ведущий '/'
user = url.username
password = url.password
host = url.hostname
port = url.port

# Путь к SQLite базе данных
sqlite_db_path = os.path.join(os.path.dirname(__file__), '..', 'db.sqlite3')

if not os.path.exists(sqlite_db_path):
    print(f"Ошибка: файл SQLite базы данных не найден: {sqlite_db_path}")
    sys.exit(1)

print(f"Миграция данных из {sqlite_db_path} в PostgreSQL ({host}:{port}/{dbname})")

# Подключение к SQLite
sqlite_conn = sqlite3.connect(sqlite_db_path)
sqlite_conn.row_factory = sqlite3.Row  # Чтобы получать строки как словари
sqlite_cursor = sqlite_conn.cursor()

# Подключение к PostgreSQL
try:
    pg_conn = psycopg2.connect(
        dbname=dbname,
        user=user,
        password=password,
        host=host,
        port=port
    )
    pg_cursor = pg_conn.cursor()
    print("Успешное подключение к PostgreSQL")
except Exception as e:
    print(f"Ошибка подключения к PostgreSQL: {e}")
    sqlite_conn.close()
    sys.exit(1)

# Получаем список всех таблиц из SQLite
sqlite_cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
tables = [row['name'] for row in sqlite_cursor.fetchall() if not row['name'].startswith('sqlite_')]

# Мигрируем каждую таблицу
for table in tables:
    print(f"Миграция таблицы {table}...")
    
    # Получаем данные из SQLite
    sqlite_cursor.execute(f"SELECT * FROM {table};")
    rows = [dict(row) for row in sqlite_cursor.fetchall()]
    
    if not rows:
        print(f"  Таблица {table} пуста, пропускаем")
        continue
    
    # Получаем структуру таблицы из первой строки
    sample_row = rows[0]
    columns = list(sample_row.keys())
    
    # Подготовка SQL запроса для вставки в PostgreSQL
    placeholders = ', '.join(['%s'] * len(columns))
    columns_str = ', '.join([f'"{column}"' for column in columns])
    
    # Вставка данных в PostgreSQL
    for row in rows:
        values = [row[column] for column in columns]
        try:
            pg_cursor.execute(
                f'INSERT INTO "{table}" ({columns_str}) VALUES ({placeholders}) ON CONFLICT DO NOTHING;',
                values
            )
        except Exception as e:
            print(f"  Ошибка при вставке в таблицу {table}: {e}")
            continue
    
    # Сохраняем изменения для этой таблицы
    pg_conn.commit()
    print(f"  Таблица {table}: мигрировано {len(rows)} строк")

# Закрываем соединения
sqlite_conn.close()
pg_conn.close()

print("Миграция данных завершена!") 