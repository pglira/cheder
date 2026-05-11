#include "keydispatcher.h"

namespace {

bool modeMatches(KeyDispatcher::Mode bindingMode, KeyDispatcher::Mode currentMode) {
    return bindingMode == KeyDispatcher::Mode::Anywhere || bindingMode == currentMode;
}

bool modsMatch(Qt::KeyboardModifiers eventMods,
               Qt::KeyboardModifiers required,
               Qt::KeyboardModifiers forbidden) {
    return (eventMods & required) == required && (eventMods & forbidden) == 0;
}

}  // namespace

void KeyDispatcher::bind(Binding b) {
    m_bindings.append(std::move(b));
}

void KeyDispatcher::addTranslation(Translation t) {
    m_translations.append(t);
}

void KeyDispatcher::setSequenceHandler(Qt::Key key, Handler h) {
    m_seqHandlers.insert(key, std::move(h));
}

void KeyDispatcher::setSequenceTranslation(Qt::Key key, Qt::Key target) {
    m_seqTranslations.insert(key, target);
}

KeyDispatcher::Result KeyDispatcher::dispatch(QKeyEvent *event,
                                              Mode currentMode,
                                              bool inputFocused) {
    const auto key  = static_cast<Qt::Key>(event->key());
    const auto mods = event->modifiers();

    // Phase 1: bindings that fire even while the action bar's input is focused
    // (F5, Ctrl+P). Pending sequence state is preserved across these.
    for (const auto &b : m_bindings) {
        if (!b.fireWhileInputFocused) continue;
        if (b.key != key) continue;
        if (!modsMatch(mods, b.requiredMods, b.forbiddenMods)) continue;
        if (!modeMatches(b.mode, currentMode)) continue;
        b.handler();
        return {Result::Handled};
    }

    if (inputFocused) return {};  // Pass

    // Phase 2: complete a pending sequence (gg / dd). Sequences require no
    // modifiers so the second press matches cleanly.
    if (m_pending != Qt::Key_unknown && key == m_pending && mods == Qt::NoModifier) {
        const Qt::Key seqKey = m_pending;
        m_pending = Qt::Key_unknown;
        if (m_seqHandlers.contains(seqKey)) {
            m_seqHandlers[seqKey]();
            return {Result::Handled};
        }
        if (m_seqTranslations.contains(seqKey)) {
            const Qt::Key translated = m_seqTranslations[seqKey];
            // View bindings first (uncommon for sequence translations); if
            // none match, fall through to synthetic dispatch so the focused
            // widget can handle the synthesized key.
            for (const auto &b : m_bindings) {
                if (b.fireWhileInputFocused) continue;
                if (b.key != translated) continue;
                if (!modsMatch(Qt::NoModifier, b.requiredMods, b.forbiddenMods)) continue;
                if (!modeMatches(b.mode, currentMode)) continue;
                b.handler();
                return {Result::Handled};
            }
            return {Result::NeedsSynthetic, translated, Qt::NoModifier};
        }
    }

    // Phase 3: any other key cancels pending.
    m_pending = Qt::Key_unknown;

    // Phase 4: start a new sequence if this key with no mods has one registered.
    if (mods == Qt::NoModifier &&
        (m_seqHandlers.contains(key) || m_seqTranslations.contains(key))) {
        m_pending = key;
        return {Result::Handled};
    }

    // Phase 5: apply translations (hjkl → arrows, Shift+G → End).
    Qt::Key effectiveKey = key;
    Qt::KeyboardModifiers effectiveMods = mods;
    for (const auto &t : m_translations) {
        if (t.from != key) continue;
        if ((mods & t.requiredMods) != t.requiredMods) continue;
        effectiveKey = t.to;
        effectiveMods = mods & ~t.stripMods;
        break;
    }

    // Phase 6: view bindings (post-translation).
    for (const auto &b : m_bindings) {
        if (b.fireWhileInputFocused) continue;
        if (b.key != effectiveKey) continue;
        if (!modsMatch(effectiveMods, b.requiredMods, b.forbiddenMods)) continue;
        if (!modeMatches(b.mode, currentMode)) continue;
        b.handler();
        return {Result::Handled};
    }

    // Translated but unbound: caller does synthetic dispatch (e.g. so a
    // QListWidget's default arrow-key navigation runs).
    if (effectiveKey != key) {
        return {Result::NeedsSynthetic, effectiveKey, effectiveMods};
    }

    return {};  // Pass
}
