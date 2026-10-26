// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesign_menu_style.hpp"
#include "maininterface/mainctx.hpp"
#include "util/color_scheme_model.hpp"
#include "addons/addon_theme_service.hpp"

#include <QApplication>
#include <QMenu>
#include <QStyleHints>
#include <QPainterPath>
#include <QRegion>
#include <QSet>
#include <QProxyStyle>
#include <QCommonStyle>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QPainter>
#include <QEvent>
#include <QScopedValueRollback>
#include <QStringList>
#include <QScreen>

namespace {
QStyle* menuBaseStyle()
{
    if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        return fusion;

    // Some contrib builds omit Fusion. A null QProxyStyle base would fall
    // back to the application style. Windows 11 menu polish changes window
    // flags and can re-enter stylesheet replacement while setStyle is active.
    return new QCommonStyle;
}

QString shortcutText(const QString& text)
{
    auto chords = text.split(QStringLiteral(", "));
    for (auto& chord : chords) {
        const bool plusKey = chord.endsWith(QStringLiteral("++"));
        // Native modifier symbols can sit directly beside a literal plus key.
        if (!plusKey && chord.endsWith(QLatin1Char('+'))) continue;
        if (plusKey) chord.chop(1);
        if (chord != QLatin1String("+"))
            chord.replace(QLatin1Char('+'), QStringLiteral(" + "));
        if (plusKey) chord += QLatin1Char('+');
    }
    return chords.join(QStringLiteral(", "));
}

class RedesignMenuMask final : public QObject
{
public:
    explicit RedesignMenuMask(QMenu* menu) : QObject(menu) { menu->installEventFilter(this); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            auto* menu = static_cast<QMenu*>(object);
            QPainterPath outline;
            const auto radius = menu->property("redesignMenuRadius").toReal();
            outline.addRoundedRect(menu->rect(), radius, radius);
            menu->setMask(QRegion(outline.toFillPolygon().toPolygon()));
            if (menu->isVisible() && menu->screen()) {
                const auto available = menu->screen()->availableGeometry();
                auto position = menu->pos();
                position.setY(qBound(available.top(), position.y(),
                    qMax(available.top(), available.bottom() - menu->height() + 1)));
                if (position != menu->pos()) menu->move(position);
            }
        }
        return false;
    }
};

class RedesignMenuStyle final : public QProxyStyle
{
public:
    explicit RedesignMenuStyle(QObject* owner) : QProxyStyle(menuBaseStyle())
    {
        setParent(owner);
        setObjectName(QStringLiteral("redesignMenuStyle"));
    }
    void configure(MainCtx* ctx)
    {
        const auto scheme = ctx->getColorScheme()->currentScheme();
        const bool light = scheme == ColorSchemeModel::Day ||
            (scheme == ColorSchemeModel::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
        m_scale = ctx->getIntfScaleFactor();
        m_compact = ctx->compactMenus();
        const auto service = vlc::addons::AddonThemeService::instance();
        const auto snapshot = service ? service->tokens(light, {}, false, !ctx->backdropBlurRequested())
            : vlc::addons::AddonThemeService::resolve({}, light, ctx->redesignAccent().name(), false, !ctx->backdropBlurRequested());
        const auto colors = snapshot.value(QStringLiteral("colors")).toMap();
        const auto color = [&colors](const char* name) { return QColor(colors.value(QString::fromLatin1(name)).toString()); };
        m_background = color("surface.menu");
        m_background.setAlphaF(snapshot.value(QStringLiteral("effects")).toMap().value(QStringLiteral("panelOpacity")).toDouble());
        m_foreground = color("text.secondary");
        m_shortcutForeground = color("text.muted");
        m_disabled = color("text.disabled");
        m_hover = color("surface.menuHover");
        m_border = color("border.menu");
        m_accent = color("accent.primary");
        const auto shape = snapshot.value(QStringLiteral("shape")).toMap();
        m_menuRadius = shape.value(QStringLiteral("menuRadius")).toDouble();
        m_controlRadius = shape.value(QStringLiteral("controlRadius")).toDouble();
    }
    int dp(int value) const { return qMax(1, qRound(value * m_scale)); }
    int menuRadius() const { return qMax(0, qRound(m_menuRadius * m_scale)); }
    QPalette menuPalette(QPalette palette) const
    {
        palette.setColor(QPalette::Window, m_background);
        palette.setColor(QPalette::Base, m_background);
        palette.setColor(QPalette::Text, m_foreground);
        palette.setColor(QPalette::WindowText, m_foreground);
        palette.setColor(QPalette::ButtonText, m_foreground);
        palette.setColor(QPalette::Highlight, m_hover);
        palette.setColor(QPalette::HighlightedText, m_foreground);
        palette.setColor(QPalette::Disabled, QPalette::Text, m_disabled);
        return palette;
    }
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const override
    {
        if (type == CT_MenuItem) {
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (item) {
                if (item->menuItemType == QStyleOptionMenuItem::Separator) return QSize(1, dp(1));
                if (item->menuItemType == QStyleOptionMenuItem::Normal || item->menuItemType == QStyleOptionMenuItem::DefaultItem || item->menuItemType == QStyleOptionMenuItem::SubMenu) {
                    const auto label = item->text.section(QLatin1Char('\t'), 0, 0);
                    const auto rawHint = item->text.section(QLatin1Char('\t'), 1);
                    const auto hint = shortcutText(rawHint);
                    // QMenu adds the shared shortcut column after it measures each item.
                    const int trailing = item->menuItemType == QStyleOptionMenuItem::SubMenu ? dp(m_compact ? 28 : 44) : hint.isEmpty() ? dp(12) : dp(28);
                    const int hintExtra = item->fontMetrics.horizontalAdvance(hint) - item->fontMetrics.horizontalAdvance(rawHint);
                    return QSize(textInset(item) + item->fontMetrics.horizontalAdvance(label) + trailing + hintExtra, dp(m_compact ? 28 : 48));
                }
            }
        }
        return QProxyStyle::sizeFromContents(type, option, size, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override
    {
        switch (metric) {
        case PM_MenuPanelWidth: return 0;
        case PM_MenuHMargin: case PM_MenuVMargin: return dp(m_compact ? 2 : 4);
        case PM_SmallIconSize: return dp(24);
        case PM_MenuScrollerHeight: return dp(24);
        default: return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* data = nullptr) const override
    {
        if (hint == SH_Menu_Scrollable) return true;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (element == PE_PanelMenu) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setCompositionMode(QPainter::CompositionMode_Source);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_background);
            painter->drawRoundedRect(option->rect, menuRadius(), menuRadius());
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override
    {
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (element != CE_MenuItem || !item ||
            (item->menuItemType != QStyleOptionMenuItem::Normal && item->menuItemType != QStyleOptionMenuItem::DefaultItem &&
             item->menuItemType != QStyleOptionMenuItem::SubMenu && item->menuItemType != QStyleOptionMenuItem::Separator)) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            painter->fillRect(item->rect, m_border);
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setLayoutDirection(item->direction);
        QRect rect = item->rect;
        if (widget) rect.setRight(qMin(rect.right(), widget->width() - dp(m_compact ? 2 : 4) - 1));
        if (item->state & State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_hover);
            painter->drawRoundedRect(rect, m_controlRadius * m_scale, m_controlRadius * m_scale);
        }
        const bool enabled = item->state & State_Enabled;
        const QColor foreground = enabled ? m_foreground : m_disabled;
        const auto visual = [&](const QRect& box) { return visualRect(item->direction, rect, box); };
        const QRect icon = visual(QRect(rect.x() + dp(m_compact ? 4 : 14), rect.center().y() - dp(12), dp(24), dp(24)));
        if (!m_compact && !item->icon.isNull()) {
            const auto state = item->checked ? QIcon::On : QIcon::Off;
            if (enabled) item->icon.paint(painter, icon, Qt::AlignCenter, QIcon::Normal, state);
            else {
                // Use the text's disabled colour instead of Qt's generated icon tint.
                auto pixmap = item->icon.pixmap(icon.size(), painter->device()->devicePixelRatioF(), QIcon::Normal, state);
                QPainter tint(&pixmap);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(pixmap.rect(), m_disabled);
                tint.end();
                drawItemPixmap(painter, icon, Qt::AlignCenter, pixmap);
            }
        }
        else if (item->checked) {
            painter->setPen(QPen(enabled ? m_accent : m_disabled, 1.5 * m_scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            QPainterPath check;
            check.moveTo(icon.x() + 6 * m_scale, icon.y() + 12 * m_scale);
            check.lineTo(icon.x() + 10 * m_scale, icon.y() + 16 * m_scale);
            check.lineTo(icon.x() + 18 * m_scale, icon.y() + 8 * m_scale);
            painter->drawPath(check);
        }
        const auto label = item->text.section(QLatin1Char('\t'), 0, 0);
        const auto hint = shortcutText(item->text.section(QLatin1Char('\t'), 1));
        int trailing = dp(12);
        painter->setFont(item->font);
        if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
            const QRect arrow = visual(QRect(rect.right() - dp(m_compact ? 25 : 37) + 1, rect.center().y() - dp(12), dp(24), dp(24)));
            painter->setPen(QPen(foreground, 1.5 * m_scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath chevron;
            const bool rtl = item->direction == Qt::RightToLeft;
            chevron.moveTo(arrow.x() + (rtl ? 14 : 10) * m_scale, arrow.y() + 7 * m_scale);
            chevron.lineTo(arrow.x() + (rtl ? 9 : 15) * m_scale, arrow.y() + 12 * m_scale);
            chevron.lineTo(arrow.x() + (rtl ? 14 : 10) * m_scale, arrow.y() + 17 * m_scale);
            painter->drawPath(chevron);
            trailing = dp(m_compact ? 28 : 44);
        } else if (!hint.isEmpty()) {
            const int width = item->fontMetrics.horizontalAdvance(hint);
            const QColor hintColor = enabled ? m_shortcutForeground : m_disabled;
            painter->setPen(hintColor);
            painter->drawText(visual(QRect(rect.right() - dp(12) - width + 1, rect.y(), width, rect.height())), Qt::AlignVCenter | Qt::AlignLeading, hint);
            trailing += width + dp(16);
        }
        painter->setPen(foreground);
        const QRect text = visual(rect.adjusted(textInset(item), 0, -trailing, 0));
        const int flags = Qt::AlignVCenter | Qt::AlignLeading |
            (styleHint(SH_UnderlineShortcut, item, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic);
        painter->drawText(text, flags, item->fontMetrics.elidedText(label, Qt::ElideRight, text.width()));
        painter->restore();
    }
private:
    int textInset(const QStyleOptionMenuItem* item) const
    {
        if (!m_compact) return dp(49);
        return dp(item->menuHasCheckableItems ? 32 : 12);
    }
    bool m_compact = false;
    qreal m_scale = 1;
    qreal m_menuRadius = 10, m_controlRadius = 6;
    QColor m_background, m_foreground, m_shortcutForeground, m_disabled, m_hover, m_border, m_accent;
};

}

void styleRedesignMenus(QMenu* root, MainCtx* ctx)
{
    const auto dp = [scale = ctx->getIntfScaleFactor()](int value) { return qMax(1, qRound(value * scale)); };
    QFont font = qApp->font();
    font.setPixelSize(dp(14));
    QSet<QMenu*> visited;
    const auto apply = [&](auto&& self, QMenu* menu) -> void {
        if (!menu || visited.contains(menu)) return;
        visited.insert(menu);
        auto* style = static_cast<RedesignMenuStyle*>(menu->findChild<QProxyStyle*>(QStringLiteral("redesignMenuStyle"), Qt::FindDirectChildrenOnly));
        if (!style) style = new RedesignMenuStyle(menu);
        style->configure(ctx);
        // Qt resizes visible menus to their full size hint when an action or
        // font changes. Keep that resize bounded; Qt still owns the scrollers.
        if (const auto* screen = menu->screen()) {
            const int frame = style->pixelMetric(QStyle::PM_MenuDesktopFrameWidth, nullptr, menu);
            menu->setMaximumHeight(qMax(1, screen->availableGeometry().height() - 2 * frame));
        }
        menu->setStyle(style);
        menu->setPalette(style->menuPalette(menu->palette()));
        menu->setFont(font);
        const int width = dp(ctx->compactMenus() ? 220 : 292);
        if (menu->property("redesignFixedMenuWidth").toBool()) menu->setFixedWidth(width);
        else menu->setMinimumWidth(width);
        menu->setProperty("redesignCompactMenu", ctx->compactMenus());
        menu->setWindowFlag(Qt::NoDropShadowWindowHint);
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setProperty("redesignMenuRadius", style->menuRadius());
        QPainterPath outline;
        outline.addRoundedRect(menu->rect(), style->menuRadius(), style->menuRadius());
        menu->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        if (!menu->property("redesignMenuMaskInstalled").toBool()) {
            new RedesignMenuMask(menu);
            menu->setProperty("redesignMenuMaskInstalled", true);
        }
        for (auto* action : menu->actions()) self(self, action->menu());
    };
    apply(apply, root);
}

namespace {
// Include menus created by Qt itself (for example, text-field context menus)
// and menus populated after the player menu was first built.
class RedesignMenuWatcher final : public QObject
{
public:
    explicit RedesignMenuWatcher(MainCtx* ctx) : QObject(ctx), m_ctx(ctx)
    {
        qApp->installEventFilter(this);
        connect(ctx, &MainCtx::compactMenusChanged, this, [this] { refresh(); });
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, [this] { refresh(); });
        connect(ctx, &MainCtx::backdropBlurEnabledChanged, this, [this] { refresh(); });
        connect(ctx, &MainCtx::redesignAccentChanged, this, [this] { refresh(); });
        connect(vlc::addons::AddonThemeService::notifications(), &vlc::addons::AddonThemeNotifications::changed, this, [this] { refresh(); });
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, [this] { refresh(); });
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { refresh(); });
    }
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (m_updating || (event->type() != QEvent::Polish && event->type() != QEvent::Show))
            return false;
        auto* menu = qobject_cast<QMenu*>(object);
        if (menu) {
            QScopedValueRollback guard(m_updating, true);
            const bool changed = menu->property("redesignCompactMenu") != QVariant(m_ctx->compactMenus());
            styleRedesignMenus(menu, m_ctx);
            if (changed && event->type() == QEvent::Show) menu->adjustSize();
        }
        return false;
    }
private:
    void refresh()
    {
        QScopedValueRollback guard(m_updating, true);
        for (auto* widget : QApplication::allWidgets()) {
            auto* menu = qobject_cast<QMenu*>(widget);
            if (!menu) continue;
            styleRedesignMenus(menu, m_ctx);
            if (menu->isVisible()) menu->adjustSize();
        }
    }
    MainCtx* m_ctx;
    bool m_updating = false;
};
}

void watchRedesignMenus(MainCtx* ctx)
{
    new RedesignMenuWatcher(ctx);
}
