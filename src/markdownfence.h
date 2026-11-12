#pragma once

#include <QStringView>

class MarkdownFence
{
public:
    enum class Line { Text, Opening, Code, Closing };

    // Qt reserves -1 for an unset block state. Signed runs preserve both the
    // marker and its length so editing an opener invalidates following blocks.
    explicit MarkdownFence(int blockState = 0)
        : m_state(blockState == -1 ? 0 : blockState) {}

    bool active() const { return m_state != 0; }
    int blockState() const { return m_state; }

    Line advance(QStringView line)
    {
        const auto isPadding = [](QChar c) { return c == u' ' || c == u'\t'; };
        int i = 0;
        while (i < line.size() && isPadding(line[i])) {
            ++i;
        }
        if (i == line.size() || (line[i] != u'`' && line[i] != u'~')) {
            return active() ? Line::Code : Line::Text;
        }
        const QChar marker = line[i];
        const int start = i;
        while (i < line.size() && line[i] == marker) {
            ++i;
        }
        const int length = i - start;
        if (!active()) {
            if (length < 3) {
                return Line::Text;
            }
            m_state = marker == u'~' ? -length : length;
            return Line::Opening;
        }
        if ((m_state < 0) != (marker == u'~') || length < qAbs(m_state)) {
            return Line::Code;
        }
        while (i < line.size() && isPadding(line[i])) {
            ++i;
        }
        if (i != line.size()) {
            return Line::Code;
        }
        m_state = 0;
        return Line::Closing;
    }

private:
    int m_state = 0;
};
