#include "queue-diff.h"

#include <QHash>
#include <QSet>

QueueApplyResult applyQueueChanges(QList<SongEntry> &queue,
                                   const QList<SongEntry> &changes,
                                   int newLength)
{
    QueueApplyResult result;
    if (newLength < 0 || changes.size() > newLength)
        return result;

    // Записи диффа обязаны описывать разные строки одной очереди. Повтор позиции
    // или идентификатора означает, что мы читаем не тот ответ (или сервер прислал
    // ерунду) — разбираться не будем, пусть вызывающий перечитает очередь.
    QSet<int> changedIds;
    QSet<int> changedPositions;
    for (const SongEntry &c : changes) {
        if (c.id < 0 || c.pos < 0 || c.pos >= newLength)
            return result;
        if (changedIds.contains(c.id) || changedPositions.contains(c.pos))
            return result;
        changedIds.insert(c.id);
        changedPositions.insert(c.pos);
    }

    // Строки, которых нет в диффе: сервер их не тронул, позиция прежняя. Копия без
    // идентификатора не свёртывается ни с чем — это сигнал, что локальное состояние
    // нам больше не доверяем.
    QList<int> stableRows;
    QHash<int, int> rowOfId;
    stableRows.reserve(queue.size());
    for (int i = 0; i < queue.size(); ++i) {
        if (queue[i].id < 0)
            return result;
        rowOfId.insert(queue[i].id, i);
        if (!changedIds.contains(queue[i].id))
            stableRows.append(i);
    }

    const int freeSlots = newLength - changes.size();
    if (stableRows.size() < freeSlots)
        return result;   // строк меньше, чем свободных мест: дифф не покрывает очередь

    // Не поместившиеся строки — удалённые. Хвостом они оказываются не случайно:
    // удаление сдвигает всё, что ниже, и эти сдвинутые строки сервер обязан
    // прислать в диффе. Значит стабильная строка не может стоять ниже удалённой —
    // отбрасывание хвоста никогда не выбрасывает живую строку (замер: delete 0 из
    // [6,1,2,4,5] даёт дифф {1@0,2@1,4@2,5@3}, стабильных строк не осталось).
    QList<SongEntry> rebuilt(newLength);
    for (const SongEntry &c : changes)
        rebuilt[c.pos] = c;

    int nextStable = 0;
    for (int pos = 0; pos < newLength; ++pos) {
        if (changedPositions.contains(pos))
            continue;
        rebuilt[pos] = queue[stableRows[nextStable++]];
    }

    // Те же трэки на тех же местах, изменились только теги — об этом стоит сказать
    // модели отдельно: reset сбросил бы выделение и прокрутку без всякой причины.
    const bool nothingDropped = stableRows.size() == freeSlots;
    const bool sameLength = queue.size() == newLength;
    if (changes.isEmpty() && nothingDropped && sameLength) {
        result.ok = true;
        result.kind = QueueApplyResult::Kind::None;
        return result;
    }

    bool tagsOnly = nothingDropped && sameLength;
    if (tagsOnly) {
        for (const SongEntry &c : changes) {
            const auto it = rowOfId.constFind(c.id);
            if (it == rowOfId.constEnd() || it.value() != c.pos) {
                tagsOnly = false;   // трэк приехал из другой строки или появился новый
                break;
            }
        }
    }

    for (int i = 0; i < rebuilt.size(); ++i)
        rebuilt[i].pos = i;   // pos == номер строки, как в полном списке очереди

    queue = std::move(rebuilt);

    result.ok = true;
    if (tagsOnly) {
        result.kind = QueueApplyResult::Kind::TagsOnly;
        for (const SongEntry &c : changes)
            result.tagRows.append(c.pos);
    } else {
        result.kind = QueueApplyResult::Kind::Structural;
    }
    return result;
}
