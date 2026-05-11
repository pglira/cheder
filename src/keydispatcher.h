#pragma once

#include <QHash>
#include <QKeyEvent>
#include <QVector>
#include <Qt>

#include <functional>

// Translates keyboard events into action invocations for cheder. Pending
// state for multi-key sequences (gg, dd) lives here so MainWindow's event
// filter collapses to "feed event in, react to result."
//
// Registration shapes:
//   - bind(): single-key shortcut with optional modifier requirements and
//     a view-mode predicate (Anywhere / Thumbnail / Image).
//   - setSequenceHandler(key, h): same-key-twice sequence (dd) that fires
//     a handler.
//   - setSequenceTranslation(key, target): same-key-twice sequence (gg)
//     that synthesizes a different key. The dispatcher then looks up
//     bindings for the synthesized key; if none match, it asks the caller
//     to send a synthetic event so the focused widget's built-in handling
//     can pick it up.
//   - addTranslation(): single-keypress rewrites (hjkl → arrows;
//     Shift+G → End). Applied before view-binding lookup.
//
// dispatch() returns a Result. NeedsSynthetic means the caller should
// build a QKeyEvent from syntheticKey + syntheticMods and send it to the
// focused widget — that's how vim-style nav reaches a QListWidget.
class KeyDispatcher {
public:
    enum class Mode { Anywhere, Thumbnail, Image };

    using Handler = std::function<void()>;

    struct Binding {
        Qt::Key                 key;
        Qt::KeyboardModifiers   requiredMods          = Qt::NoModifier;  // must be set
        Qt::KeyboardModifiers   forbiddenMods         = Qt::NoModifier;  // must NOT be set
        Mode                    mode                  = Mode::Anywhere;
        bool                    fireWhileInputFocused = false;
        Handler                 handler;
    };

    struct Translation {
        Qt::Key                 from;
        Qt::KeyboardModifiers   requiredMods = Qt::NoModifier;
        Qt::Key                 to;
        Qt::KeyboardModifiers   stripMods    = Qt::NoModifier;
    };

    void bind(Binding b);
    void addTranslation(Translation t);
    void setSequenceHandler(Qt::Key key, Handler h);
    void setSequenceTranslation(Qt::Key key, Qt::Key target);

    struct Result {
        enum Status { Pass, Handled, NeedsSynthetic };
        Status                status        = Pass;
        Qt::Key               syntheticKey  = Qt::Key_unknown;
        Qt::KeyboardModifiers syntheticMods = Qt::NoModifier;
    };

    Result dispatch(QKeyEvent *event, Mode currentMode, bool inputFocused);

private:
    QVector<Binding>        m_bindings;
    QVector<Translation>    m_translations;
    QHash<Qt::Key, Handler> m_seqHandlers;        // dd → handler
    QHash<Qt::Key, Qt::Key> m_seqTranslations;    // gg → Home
    Qt::Key                 m_pending = Qt::Key_unknown;
};
