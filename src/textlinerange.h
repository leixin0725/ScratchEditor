#pragma once

#include <QString>

struct LineRange {
    int start = 0;
    int end = 0;
};

inline LineRange lineRangeAt(const QString &text, int position)
{
    const int boundedPosition = qBound(0, position, text.size());
    // QString::lastIndexOf(..., -1) 会从文档末尾反向搜索。文档以空行开头时，
    // 位置 0 必须显式归属于首个空块，不能把首个换行误当成前置分隔符。
    const int start = boundedPosition == 0
        ? 0 : text.lastIndexOf(QLatin1Char('\n'), boundedPosition - 1) + 1;
    int end = text.indexOf(QLatin1Char('\n'), boundedPosition);
    if (end < 0) {
        end = text.size();
    }
    return {start, end};
}
