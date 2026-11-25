# Минимальное остовное дерево

Консольное приложение строит минимальное остовное дерево простого взвешенного неориентированного графа алгоритмами Прима и Крускала. Оно восстанавливает единственный путь между вершинами дерева, проверяет минимальность по условию обмена и экспортирует измерения времени и памяти. Для несвязного графа оба алгоритма возвращают ошибку.

## Сборка

Основная среда — Linux x86-64, GCC 16 или новее с libstdc++, CMake 3.30 или новее и Ninja. Нужна реализация C++23 в стандартной библиотеке, включая `std::expected`, `std::print` и `std::views::enumerate`. Сборка использует `-std=c++26`. Пресет `clang` проверяет переносимость на Clang с совместимой стандартной библиотекой.

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

CMake использует установленный Catch2 3.11.0 или скачивает архив этой версии с проверкой SHA-256. Для автономной сборки CLI используйте `cmake --preset release -DBUILD_TESTING=OFF`. С локальной копией исходников Catch2 укажите `-DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2-3.11.0`.

`debug`, `release`, `asan`, `ubsan` и `clang` используют профиль Portable. `cpp26-full` включает рефлексию и контракты с принудительной проверкой после успешных компиляционных проб. Оба профиля используют доступные библиотечные возможности C++26.

## Входной файл

Первая строка содержит число вершин `n` и число рёбер `m`. За ней следуют ровно `m` строк вида `u v weight`. Вершины нумеруются от 1 до `n`. Петли и повторные неупорядоченные пары запрещены. После последней записи допустимы пробельные строки.

```text
3 3
1 2 -4
2 3 0
1 3 7
```

По умолчанию веса имеют тип `int64`. Для конечных вещественных весов, в том числе записи с экспонентой, используйте `--weight-type double`. Ошибки чтения и данных содержат тип и описание, а для ошибочных записей файла — номер строки и позицию. Коды выхода 0, 1, 2 и 3 означают соответственно успех, ошибку данных или ввода-вывода, ошибку использования команды и исключительную ситуацию.

## Запуск

```bash
build/release/mst
build/release/mst --help
build/release/mst solve examples/chapter1.txt --algorithm prim --start 1 --trace --path 1 6
build/release/mst solve examples/chapter1.txt --algorithm kruskal --trace --validate
build/release/mst compare examples/chapter1.txt --weight-type double
build/release/mst generate --vertices 1000 --edges 5000 --seed 42 --output graph.txt
```

На графе из курсовой вес остова равен 11, путь `1 → 3 → 2 → 4 → 5 → 6` также весит 11. В интерактивном режиме пункты 6 и 7 работают с сохранённым деревом. Успешная загрузка другого графа сбрасывает дерево.

## Тесты и эксперименты

```bash
cmake --preset asan
cmake --build --preset asan
ctest --preset asan

build/release/mst benchmark --config benchmarks/configs/smoke.conf --output /tmp/mst-smoke.csv
build/release/mst benchmark --config benchmarks/configs/coursework.conf --output /tmp/mst-coursework.csv
```

Те же команды сборки и проверок доступны для `ubsan`, `clang` и `cpp26-full`. Подробности приведены в [описании проверок](docs/testing.md).

Экспорт создаёт основной CSV, файл `*.raw.csv` с отдельными замерами и `*.metadata.csv` с параметрами среды. [Формат конфигурации, методика и результаты](docs/experiments.md).

- [Архитектура и UML](docs/architecture.md)
- [Алгоритмы и инварианты](docs/algorithms.md)
- [Возможности C++26](docs/cpp26-features.md)
