#include "commentsetting.h"

bool CommentSetting::useCommentNodeColor;
bool CommentSetting::useCommentNodeRadius;
bool CommentSetting::appendComment;
std::vector<CommentSetting> CommentSetting::comments;

CommentSetting::CommentSetting(const QString shortcut, const QString text, const QColor color, const float nodeRadius) :
    shortcut(shortcut), text(text), color(color), nodeRadius(nodeRadius) { }

QColor CommentSetting::getColor(const QString comment) {
    for(const auto item : comments) {
        if(!item.text.isEmpty() && comment.contains(item.text, Qt::CaseInsensitive)) {
            return item.color;
        }
    }
    return Qt::yellow;
}

float CommentSetting::getRadius(const QString comment) {
    for(const auto item : comments) {
        if(!item.text.isEmpty() && comment.contains(item.text, Qt::CaseInsensitive)) {
            return item.nodeRadius;
        }
    }
    return 0;
}
