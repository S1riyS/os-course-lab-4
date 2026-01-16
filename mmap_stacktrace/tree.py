import sys
import re

def extract_call_tree(text):
    """Извлекает дерево вызовов с сохранением структуры"""
    lines = text.strip().split('\n')
    result = []
    
    for line in lines:
        # Пропускаем заголовки и пустые строки
        if line.startswith('#') or not line.strip():
            continue
        
        # Проверяем, содержит ли строка информацию о вызове функции
        # Ищем паттерн с '|' и фигурными скобками
        if '|' in line and ('()' in line or '{' in line or '}' in line):
            # Извлекаем только часть с функцией (после последнего '| ')
            parts = line.split('|')
            
            # Берем последнюю часть, содержащую вызов функции
            func_part = parts[-1].strip() if parts else ''
            
            if func_part:
                # Очищаем от времени выполнения (если есть)
                # Удаляем временные метки типа "0.500 us", "10.958 us", "+ 10.958 us"
                func_part = re.sub(r'[\+\-]?\s*\d+\.\d+\s+us\s*', '', func_part)
                func_part = re.sub(r'\d+\.\d+\s+us\s*', '', func_part)
                func_part = func_part.strip()
                
                # Если строка не пустая после очистки
                if func_part:
                    result.append(func_part)
    
    return result

def format_call_tree(tree_lines):
    """Форматирует дерево вызовов с правильными отступами"""
    formatted = []
    indent_level = 0
    
    for line in tree_lines:
        # Определяем, является ли строка открывающей или закрывающей скобкой
        line_stripped = line.strip()
        
        if line_stripped.endswith('{'):
            # Открывающая скобка - выводим с текущим отступом
            func_name = line_stripped[:-1].strip()
            formatted.append('  ' * indent_level + func_name + ' {')
            indent_level += 1
        elif line_stripped == '}':
            # Закрывающая скобка - уменьшаем отступ
            indent_level = max(0, indent_level - 1)
            formatted.append('  ' * indent_level + '}')
        elif '()' in line_stripped:
            # Вызов функции без скобок (вероятно, вложенный вызов)
            formatted.append('  ' * indent_level + line_stripped)
    
    return formatted

def main():
    # Чтение входных данных
    if len(sys.argv) > 1:
        # Чтение из файла
        with open(sys.argv[1], 'r', encoding='utf-8') as f:
            text = f.read()
    else:
        # Чтение из stdin
        text = sys.stdin.read()
    
    # Извлекаем дерево вызовов
    call_tree = extract_call_tree(text)
    
    # Форматируем с отступами
    formatted_tree = format_call_tree(call_tree)
    
    # Выводим результат
    for line in formatted_tree:
        print(line)

if __name__ == "__main__":
    main()