# Проверки

Catch2 3.11.0 подключён к CTest. Тесты не требуют доступа к сети после сборки зависимости.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
ctest --preset debug -R 'Chapter one|enumeration'
```

- Модульные тесты проверяют DSU, типы идентификаторов, валидацию строк и графов, целочисленное переполнение, вещественные суммы и PMR-метрики ресурса.
- Демонстрационный тест проверяет порядок принятия рёбер обоими алгоритмами, отклонение ребра 3–4 Крускалом, вес 11 и маршрут 1–3–2–4–5–6.
- Тесты пути включают одноэлементное дерево, совпадающие и некорректные вершины, отрицательные и нулевые веса и цепь из 100 000 вершин. Отдельный случай проверяет различие между путём в дереве и кратчайшим путём исходного графа.
- Воспроизводимые проверки свойств строят случайные малые графы, остовы для каждой стартовой вершины и переставленного входа. Они сравнивают веса с полным перебором и применяют обменную проверку.
- Интеграционный сценарий запускает настоящий CLI, проверяет коды выхода, интерактивное повторное использование дерева, сброс состояния, генерацию и файлы экспериментов.

```bash
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset ubsan && cmake --build --preset ubsan && ctest --preset ubsan
cmake --preset clang && cmake --build --preset clang && ctest --preset clang
cmake --preset cpp26-full && cmake --build --preset cpp26-full && ctest --preset cpp26-full

rg --files include src/app src/core src/benchmark tests -g '*.hpp' -g '*.cpp' |
  xargs clang-format --dry-run --Werror
clang-tidy -p build/clang src/core/error.cpp src/benchmark/benchmark.cpp src/app/main.cpp \
  tests/unit/core.cpp tests/property/random.cpp
```
