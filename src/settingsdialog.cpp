#include <QPointer>
/*
    RPG Forge
    Copyright (C) 2026  Sheldon Lee Wen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "settingsdialog.h"
#include "prompteditordialog.h"
#include "toggleswitch.h"
#include "draghandle.h"
#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QTabWidget>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QSettings>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QLabel>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Configure RPG Forge"));
    setupUi();
    load();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);

    m_tabWidget->addTab(createLLMTab(), i18n("LLM Providers"));
    m_tabWidget->addTab(createAgentsTab(), i18n("AI Services"));
    m_tabWidget->addTab(createPromptsTab(), i18n("System Prompts"));
    m_tabWidget->addTab(createAnalyzerTab(), i18n("Game Analyzer"));
    m_tabWidget->addTab(createEditorTab(), i18n("Editor"));

    mainLayout->addWidget(m_tabWidget);

    m_testProgressBar = new QProgressBar(this);
    m_testProgressBar->setRange(0, 0); // Indeterminate
    m_testProgressBar->hide();
    mainLayout->addWidget(m_testProgressBar);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    setMinimumSize(600, 650);
}

// ---------------------------------------------------------------------------
// Persist the user's reordered provider list + per-provider enabled flag.
// Order comes from the live row order in the QListWidget (moved around by
// drag-reorder); enabled state comes from each row's ToggleSwitch.
// ---------------------------------------------------------------------------
void SettingsDialog::saveProviderOrderList()
{
    if (!m_providerRowsLayout) return;
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QStringList orderKeys;
    for (int i = 0; i < m_providerRowsLayout->count(); ++i) {
        QLayoutItem *item = m_providerRowsLayout->itemAt(i);
        QWidget *w = item ? item->widget() : nullptr;
        if (!w) continue;
        const QVariant tag = w->property("llmProvider");
        if (!tag.isValid()) continue;
        const auto p = static_cast<LLMProvider>(tag.toInt());
        orderKeys.append(LLMService::providerKey(p));
        if (ToggleSwitch *sw = m_providerToggles.value(p, nullptr)) {
            settings.setValue(LLMService::providerSettingsKey(p) + QStringLiteral("/enabled"),
                              sw->isChecked());
        }
    }
    settings.setValue(QStringLiteral("llm/provider_order"), orderKeys);
}

// ---------------------------------------------------------------------------
// Drop-indicator helpers. During a provider-row drag, DragHandle emits
// targetIndexChanged with the layout index where the row will be inserted
// on release. We translate that to a y-coordinate in the container's
// coordinate space and position the thin red bar there.
// ---------------------------------------------------------------------------
void SettingsDialog::showDropIndicatorAtIndex(int targetIndex)
{
    if (!m_providerDropIndicator || !m_providerRowsLayout) return;
    QWidget *container = m_providerDropIndicator->parentWidget();
    if (!container) return;

    const int rowCount = m_providerRowsLayout->count();
    if (rowCount == 0) return;

    const int spacing = m_providerRowsLayout->spacing();
    int y = 0;
    if (targetIndex <= 0) {
        QWidget *first = m_providerRowsLayout->itemAt(0)->widget();
        if (first) y = first->y() - qMax(1, spacing / 2);
    } else if (targetIndex >= rowCount) {
        QWidget *last = m_providerRowsLayout->itemAt(rowCount - 1)->widget();
        if (last) y = last->y() + last->height() + qMax(1, spacing / 2)
                         - m_providerDropIndicator->height();
    } else {
        QWidget *before = m_providerRowsLayout->itemAt(targetIndex - 1)->widget();
        if (before) y = before->y() + before->height() + spacing / 2
                           - m_providerDropIndicator->height() / 2;
    }

    m_providerDropIndicator->setGeometry(
        0, y, container->width(), m_providerDropIndicator->height());
    m_providerDropIndicator->raise();
    m_providerDropIndicator->show();
}

void SettingsDialog::hideDropIndicator()
{
    if (m_providerDropIndicator) m_providerDropIndicator->hide();
}

QWidget* SettingsDialog::createLLMTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    // --- Provider stack ---
    // Each provider's credential/model block lives in a composite row
    // (grip icon | group box | toggle switch) added to a plain
    // QVBoxLayout inside a QScrollArea. DragHandle reorders by
    // removeWidget/insertWidget on this layout — no QListWidget,
    // no setItemWidget persistent-editor fragility. Row order persists
    // to llm/provider_order; the toggle state persists to
    // llm/{provider}/enabled and drives the fallback chain in
    // LLMService::composeDefaultFallbackChain.
    auto *providersScroll = new QScrollArea(this);
    providersScroll->setWidgetResizable(true);
    providersScroll->setFrameShape(QFrame::NoFrame);
    auto *providersContainer = new QWidget(providersScroll);
    m_providerRowsLayout = new QVBoxLayout(providersContainer);
    m_providerRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_providerRowsLayout->setSpacing(6);
    providersScroll->setWidget(providersContainer);
    layout->addWidget(providersScroll, /*stretch=*/1);

    // Drop-indicator overlay: a thin red bar that shows where the dragged
    // row will land on release. Child of providersContainer so its y-
    // coordinate is the same space as the row widgets' geometries.
    // raise() on show keeps it visible above siblings.
    m_providerDropIndicator = new QFrame(providersContainer);
    m_providerDropIndicator->setFrameShape(QFrame::NoFrame);
    m_providerDropIndicator->setStyleSheet(
        QStringLiteral("background-color: #e74c3c; border-radius: 1px;"));
    m_providerDropIndicator->setFixedHeight(3);
    m_providerDropIndicator->hide();

    auto *orderHint = new QLabel(
        i18n("Drag the ≡ handle to reorder providers. Toggle the switch to "
             "pause a provider — paused providers don't participate in "
             "fallback. Services pick their primary in the Agents tab."),
        this);
    orderHint->setWordWrap(true);
    QFont hintFont = orderHint->font();
    hintFont.setItalic(true);
    orderHint->setFont(hintFont);
    layout->addWidget(orderHint);

    // Helper: build one provider's composite row (grip | group box | toggle)
    // and append it as an item in m_providerListWidget. The group box holds
    // API-key (optional), endpoint, default-model combo, embedding-model
    // combo, and inline status label. Wires editingFinished on the key /
    // endpoint fields to testProviderConnection so the combos auto-populate
    // once the user has credentials.
    auto buildProviderGroup = [this](LLMProvider provider,
                                     const QString &title,
                                     const QString &endpointPlaceholder,
                                     bool hasKey,
                                     bool keyOptional,
                                     QLineEdit **keyOut,
                                     QLineEdit **endpointOut,
                                     QComboBox **modelComboOut,
                                     const QString &endpointLabel = QString()) {
        auto *group = new QGroupBox(title, this);
        auto *form = new QFormLayout(group);

        QLineEdit *keyEdit = nullptr;
        if (hasKey) {
            keyEdit = new QLineEdit(this);
            keyEdit->setEchoMode(QLineEdit::Password);
            form->addRow(keyOptional ? i18n("API Key (Optional):")
                                     : i18n("API Key:"),
                         keyEdit);
        }

        auto *endpointEdit = new QLineEdit(this);
        endpointEdit->setPlaceholderText(endpointPlaceholder);
        form->addRow(endpointLabel.isEmpty() ? i18n("Endpoint:")
                                             : endpointLabel,
                     endpointEdit);

        auto *modelCombo = new QComboBox(this);
        modelCombo->setEditable(true);
        modelCombo->setMinimumWidth(350);
        modelCombo->lineEdit()->setPlaceholderText(
            i18n("Enter API key above to auto-populate"));
        form->addRow(i18n("Default Model:"), modelCombo);

        // Per-provider embedding model combo. Populated by the same fetch;
        // filterEmbeddingModels returns only embedding-capable entries. If
        // the filter yields nothing (Anthropic/Grok don't publish
        // embedding endpoints) the combo stays disabled with a "Not
        // supported" placeholder — KnowledgeBase's chain walk will skip
        // this provider for embedding requests.
        auto *embeddingCombo = new QComboBox(this);
        embeddingCombo->setEditable(true);
        embeddingCombo->setMinimumWidth(350);
        embeddingCombo->lineEdit()->setPlaceholderText(
            i18n("Populates after key/endpoint is set"));
        form->addRow(i18n("Embedding Model:"), embeddingCombo);

        auto *statusLabel = new QLabel(QString(), this);
        statusLabel->setWordWrap(true);
        QFont sf = statusLabel->font();
        sf.setItalic(true);
        statusLabel->setFont(sf);
        form->addRow(QString(), statusLabel);

        // Compose row: [grip handle | provider group box | enable toggle].
        // The grip is a visual drag-affordance; the whole row is draggable
        // via the QListWidget's InternalMove. The toggle flips this
        // provider's participation in the fallback chain.
        auto *rowWidget = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        rowLayout->setSpacing(8);

        // Draggable grip: DragHandle reorders rows in
        // m_providerRowsLayout on mouseRelease. See draghandle.cpp.
        auto *grip = new DragHandle(m_providerRowsLayout, rowWidget);
        const QIcon gripIcon = QIcon::fromTheme(
            QStringLiteral("application-menu"),
            QIcon::fromTheme(QStringLiteral("open-menu-symbolic")));
        if (!gripIcon.isNull()) {
            grip->setPixmap(gripIcon.pixmap(16, 16));
        } else {
            // Fallback glyph when the theme has no hamburger icon.
            grip->setText(QStringLiteral("\u2630"));
            QFont gf = grip->font();
            gf.setPointSize(gf.pointSize() + 2);
            grip->setFont(gf);
        }
        grip->setToolTip(i18n("Drag to reorder fallback position"));
        grip->setFixedWidth(20);
        grip->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(grip, 0, Qt::AlignTop);

        // Live drop-position feedback. The indicator is owned by the
        // providers container (created in this same function before the
        // first buildProviderGroup call), so we can just toggle and
        // reposition it from the drag signals.
        connect(grip, &DragHandle::dragStarted, this, [this]() {
            if (m_providerDropIndicator) {
                showDropIndicatorAtIndex(0); // position will be updated immediately
            }
        });
        connect(grip, &DragHandle::targetIndexChanged, this,
                [this](int idx) { showDropIndicatorAtIndex(idx); });
        connect(grip, &DragHandle::dragReleased, this,
                [this]() { hideDropIndicator(); });
        rowLayout->addWidget(group, 1);

        auto *toggle = new ToggleSwitch(rowWidget);
        toggle->setToolTip(i18n(
            "Enable or disable %1 in the fallback chain. Disabled "
            "providers are skipped when a primary request fails.",
            title));
        m_providerToggles.insert(provider, toggle);
        rowLayout->addWidget(toggle, 0, Qt::AlignTop);

        // Tag the row widget with its provider so save-time iteration
        // can recover the identity by walking the QVBoxLayout children.
        rowWidget->setProperty("llmProvider", static_cast<int>(provider));
        m_providerRowsLayout->addWidget(rowWidget);

        m_providerModelCombos.insert(provider, modelCombo);
        m_providerEmbeddingCombos.insert(provider, embeddingCombo);
        m_providerStatusLabels.insert(provider, statusLabel);

        // Trigger the API-key-test + model-fetch when the user finishes
        // editing either credential-bearing field. KWallet write happens
        // inside testProviderConnection so the fetch uses the fresh key.
        if (keyEdit) {
            connect(keyEdit, &QLineEdit::editingFinished, this, [this, provider]() {
                testProviderConnection(provider);
            });
        }
        connect(endpointEdit, &QLineEdit::editingFinished, this, [this, provider]() {
            testProviderConnection(provider);
        });

        if (keyOut) *keyOut = keyEdit;
        if (endpointOut) *endpointOut = endpointEdit;
        if (modelComboOut) *modelComboOut = modelCombo;
    };

    // Invoke buildProviderGroup in the user's stored fallback order so the
    // rendered row order already matches QSettings on first show. Any
    // providers missing from the stored order (e.g. new enum values) fall
    // in at the tail — readProviderOrderFromSettings handles that.
    auto buildByProvider = [&](LLMProvider p) {
        switch (p) {
        case LLMProvider::OpenAI:
            buildProviderGroup(LLMProvider::OpenAI, i18n("OpenAI"),
                QStringLiteral("https://api.openai.com/v1/chat/completions"),
                /*hasKey=*/true, /*keyOptional=*/false,
                &m_openaiKeyEdit, &m_openaiEndpointEdit, &m_openaiModelCombo);
            break;
        case LLMProvider::Anthropic:
            buildProviderGroup(LLMProvider::Anthropic, i18n("Anthropic"),
                QStringLiteral("https://api.anthropic.com/v1/messages"),
                /*hasKey=*/true, /*keyOptional=*/false,
                &m_anthropicKeyEdit, &m_anthropicEndpointEdit, &m_anthropicModelCombo);
            break;
        case LLMProvider::Ollama:
            buildProviderGroup(LLMProvider::Ollama, i18n("Ollama"),
                QStringLiteral("http://localhost:11434/api/chat"),
                /*hasKey=*/false, /*keyOptional=*/false,
                /*keyOut=*/nullptr, &m_ollamaEndpointEdit, &m_ollamaModelCombo,
                i18n("Local Endpoint:"));
            break;
        case LLMProvider::Grok:
            buildProviderGroup(LLMProvider::Grok, i18n("Grok (xAI)"),
                QStringLiteral("https://api.x.ai/v1/chat/completions"),
                /*hasKey=*/true, /*keyOptional=*/false,
                &m_grokKeyEdit, &m_grokEndpointEdit, &m_grokModelCombo);
            break;
        case LLMProvider::Gemini:
            buildProviderGroup(LLMProvider::Gemini, i18n("Google"),
                QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"),
                /*hasKey=*/true, /*keyOptional=*/false,
                &m_geminiKeyEdit, &m_geminiEndpointEdit, &m_geminiModelCombo);
            break;
        case LLMProvider::LMStudio:
            buildProviderGroup(LLMProvider::LMStudio, i18n("LM Studio"),
                QStringLiteral("http://localhost:1234/v1/chat/completions"),
                /*hasKey=*/true, /*keyOptional=*/true,
                &m_lmstudioKeyEdit, &m_lmstudioEndpointEdit, &m_lmstudioModelCombo);
            break;
        }
    };

    for (LLMProvider p : LLMService::readProviderOrderFromSettings()) {
        buildByProvider(p);
    }

    return tab;
}

QWidget* SettingsDialog::createAgentsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    auto *container = new QWidget(this);
    auto *form = new QFormLayout(container);

    auto createAgentRow = [&](const QString &id, const QString &label, const QString &prefix) {
        auto *prov = new QComboBox(this);
        prov->addItems({QStringLiteral("OpenAI"), QStringLiteral("Anthropic"), QStringLiteral("Ollama"), QStringLiteral("Grok"), QStringLiteral("Google"), QStringLiteral("LM Studio")});
        
        auto *model = new QComboBox(this);
        model->setEditable(true);
        model->setMinimumWidth(350);
        
        auto *rowLayout = new QHBoxLayout();
        rowLayout->addWidget(prov);
        rowLayout->addWidget(model);
        
        form->addRow(label + QStringLiteral(":"), rowLayout);
        m_agentConfigs[id] = {prov, model, prefix};

        connect(prov, &QComboBox::currentIndexChanged, this, [this, id](int index) {
            updateModelCombos(static_cast<LLMProvider>(index));
        });
    };

    // Names here MUST match the Project Settings "AI Services" tab so users
    // recognize the same service between global model config and per-project
    // enable/disable. Simulation rows stay as-is (they're not part of the
    // per-project AI Services toggles).
    createAgentRow(QStringLiteral("analyzer"), i18n("Game Analyzer"), QStringLiteral("analyzer"));
    createAgentRow(QStringLiteral("lorekeeper"), i18n("LoreKeeper"), QStringLiteral("lorekeeper"));
    createAgentRow(QStringLiteral("synopsis_file"), i18n("Synopsis Generator (File)"), QStringLiteral("synopsis"));
    createAgentRow(QStringLiteral("synopsis_folder"), i18n("Synopsis Generator (Folder)"), QStringLiteral("synopsis"));
    createAgentRow(QStringLiteral("librarian"), i18n("Variable Librarian"), QStringLiteral("librarian"));
    createAgentRow(QStringLiteral("chat"), i18n("AI Writing Assistant"), QStringLiteral("llm"));
    createAgentRow(QStringLiteral("chargen"), i18n("Character Generator"), QStringLiteral("chargen"));
    createAgentRow(QStringLiteral("sim_arbiter"), i18n("Simulation Arbiter"), QStringLiteral("simulation"));
    createAgentRow(QStringLiteral("sim_griot"), i18n("Simulation Griot"), QStringLiteral("simulation"));
    createAgentRow(QStringLiteral("sim_actor"), i18n("Simulation Actor"), QStringLiteral("simulation"));

    scrollArea->setWidget(container);
    layout->addWidget(scrollArea);

    // Behavior toggles that apply across the AI services tab. Currently
    // just the Writing Assistant's default depth (Quick vs Comprehensive
    // multi-hop). Adding a sub-section here keeps it in the user's line
    // of sight when configuring per-service models above.
    m_chatDeepSearchCheck = new QCheckBox(
        i18n("Default to Deep Search in Writing Assistant (multi-hop retrieval)"),
        this);
    m_chatDeepSearchCheck->setToolTip(i18n(
        "When enabled, the Writing Assistant chat starts every new request "
        "in Deep Search mode (drafts an answer, identifies information "
        "gaps, retrieves more, refines — up to 3 hops). Slower and uses "
        "more tokens. The toolbar toggle on the chat panel always wins for "
        "any single request."));
    layout->addWidget(m_chatDeepSearchCheck);

    auto *testBtn = new QPushButton(i18n("Test All Agent Connections"), this);
    layout->addWidget(testBtn);
    connect(testBtn, &QPushButton::clicked, this, [this]() {
        m_testProgressBar->show();
        QStringList agentIds = m_agentConfigs.keys();
        auto *completed = new int(0);
        auto *failed = new int(0);
        
        for (const QString &id : agentIds) {
            LLMRequest req;
            req.provider = static_cast<LLMProvider>(m_agentConfigs[id].providerCombo->currentIndex());
            req.model = m_agentConfigs[id].modelCombo->currentText();
            LLMMessage msg;
            msg.role = QStringLiteral("user");
            msg.content = QStringLiteral("Respond with OK");
            req.messages << msg;
            req.stream = false;

            QPointer<SettingsDialog> weakThis(this);
            LLMService::instance().sendNonStreamingRequest(req, [weakThis, completed, failed, agentIds](const QString &response) {
                if (!weakThis) {
                    (*completed)++;
                    if (*completed == agentIds.size()) {
                        delete completed;
                        delete failed;
                    }
                    return;
                }
                (*completed)++;
                if (response.isEmpty()) (*failed)++;
                
                if (*completed == agentIds.size()) {
                    weakThis->m_testProgressBar->hide();
                    if (*failed > 0) {
                        QMessageBox::critical(weakThis, i18n("Connection Test Failed"), 
                            i18n("%1 agent(s) failed to connect. Please verify your API keys and model names.", *failed));
                    } else {
                        QMessageBox::information(weakThis, i18n("Connection Test Successful"), 
                            i18n("All agents connected successfully."));
                    }
                    delete completed;
                    delete failed;
                }
            });
        }
    });

    return tab;
}

void SettingsDialog::updateModelCombos(LLMProvider provider)
{
    if (m_modelCache.contains(provider)) {
        // Agents tab rows whose primary is this provider.
        for (auto it = m_agentConfigs.begin(); it != m_agentConfigs.end(); ++it) {
            auto &config = it.value();
            if (static_cast<LLMProvider>(config.providerCombo->currentIndex()) == provider) {
                QString current = config.modelCombo->currentText();
                config.modelCombo->clear();
                config.modelCombo->addItems(m_modelCache[provider]);
                config.modelCombo->setEditText(current);
            }
        }

        // LLM tab — the per-provider default-model combo.
        if (QComboBox *combo = m_providerModelCombos.value(provider, nullptr)) {
            const QString current = combo->currentText();
            combo->clear();
            combo->addItems(m_modelCache[provider]);
            combo->setEditText(current);
        }

        // LLM tab — the per-provider embedding-model combo. Filter the
        // fetched list for embedding-capable entries; if none found, the
        // combo is disabled with placeholder text so the user knows this
        // provider won't participate in embedding requests.
        if (QComboBox *eCombo = m_providerEmbeddingCombos.value(provider, nullptr)) {
            const QStringList embeds = LLMService::filterEmbeddingModels(
                provider, m_modelCache[provider]);
            const QString current = eCombo->currentText();
            eCombo->clear();
            eCombo->addItems(embeds);
            eCombo->setEditText(current);
            eCombo->setEnabled(!embeds.isEmpty() || !current.isEmpty());
            eCombo->lineEdit()->setPlaceholderText(
                embeds.isEmpty()
                    ? i18n("Not supported by this provider")
                    : i18n("Choose embedding model"));
        }
        return;
    }

    LLMService::instance().fetchModels(provider, [this, provider](const QStringList &models) {
        m_modelCache[provider] = models;
        updateModelCombos(provider);
    });
}

// ---------------------------------------------------------------------------
// testProviderConnection: on API-key / endpoint edit, write the new key to
// KWallet (cloud providers only) and kick off a live fetchModels call. On
// success, populate the provider's model combo and show a green "Connected
// — N models" status. On failure (empty list), show an italic warning so
// the user knows to recheck credentials.
// ---------------------------------------------------------------------------
void SettingsDialog::testProviderConnection(LLMProvider provider)
{
    QLabel *status = m_providerStatusLabels.value(provider, nullptr);

    // Persist the freshly-typed key to KWallet so fetchModels picks it up.
    // Local providers (Ollama, optional-key LMStudio) may not have a key
    // edit widget; skip the write in those cases.
    QLineEdit *keyEdit = nullptr;
    switch (provider) {
        case LLMProvider::OpenAI:    keyEdit = m_openaiKeyEdit; break;
        case LLMProvider::Anthropic: keyEdit = m_anthropicKeyEdit; break;
        case LLMProvider::Grok:      keyEdit = m_grokKeyEdit; break;
        case LLMProvider::Gemini:    keyEdit = m_geminiKeyEdit; break;
        case LLMProvider::LMStudio:  keyEdit = m_lmstudioKeyEdit; break;
        case LLMProvider::Ollama:    break; // no key
    }
    if (keyEdit && !keyEdit->text().isEmpty()) {
        LLMService::instance().setApiKey(provider, keyEdit->text());
    }

    // Also persist the endpoint so fetchModels (which reads from QSettings)
    // uses the user's freshly-typed value. Without this the test would run
    // against the stale endpoint from last save, which is exactly what we
    // don't want when the user is actively configuring.
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    QLineEdit *endpointEdit = nullptr;
    switch (provider) {
        case LLMProvider::OpenAI:    endpointEdit = m_openaiEndpointEdit; break;
        case LLMProvider::Anthropic: endpointEdit = m_anthropicEndpointEdit; break;
        case LLMProvider::Ollama:    endpointEdit = m_ollamaEndpointEdit; break;
        case LLMProvider::Grok:      endpointEdit = m_grokEndpointEdit; break;
        case LLMProvider::Gemini:    endpointEdit = m_geminiEndpointEdit; break;
        case LLMProvider::LMStudio:  endpointEdit = m_lmstudioEndpointEdit; break;
    }
    if (endpointEdit) {
        settings.setValue(LLMService::providerSettingsKey(provider) + QStringLiteral("/endpoint"),
                          endpointEdit->text());
    }

    // Invalidate the session model cache for this provider so the fetch
    // below goes to the network with the user's new credentials rather
    // than returning the stale previous list.
    m_modelCache.remove(provider);

    if (status) {
        status->setText(i18n("Testing…"));
        status->setStyleSheet(QString());
    }

    QPointer<SettingsDialog> weakThis(this);
    LLMService::instance().fetchModels(provider, [weakThis, provider](const QStringList &models) {
        if (!weakThis) return;
        weakThis->m_modelCache[provider] = models;
        weakThis->updateModelCombos(provider);

        QLabel *status = weakThis->m_providerStatusLabels.value(provider, nullptr);
        if (!status) return;
        if (models.isEmpty()) {
            status->setText(i18n("Could not fetch models. Check your API key and endpoint."));
            status->setStyleSheet(QStringLiteral("color: #c0392b;")); // red
        } else {
            status->setText(i18n("Connected — %1 models available", models.size()));
            status->setStyleSheet(QStringLiteral("color: #27ae60;")); // green
        }
    });
}

QWidget* SettingsDialog::createPromptsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    // Reusable Templates Section
    auto *templatesGroup = new QGroupBox(i18n("Editor Prompt Templates"), this);
    auto *templatesLayout = new QVBoxLayout(templatesGroup);
    m_promptsList = new QListWidget(this);
    m_promptsList->setMaximumHeight(150);
    templatesLayout->addWidget(m_promptsList);

    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(i18n("Add Template..."), this);
    auto *viewTemplateBtn = new QPushButton(i18n("View"), this);
    auto *editTemplateBtn = new QPushButton(i18n("Edit"), this);
    auto *removeBtn = new QPushButton(i18n("Remove"), this);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(viewTemplateBtn);
    btnLayout->addWidget(editTemplateBtn);
    btnLayout->addWidget(removeBtn);
    templatesLayout->addLayout(btnLayout);
    layout->addWidget(templatesGroup);

    // Core System Prompts Section
    auto *coreGroup = new QGroupBox(i18n("Core Engine System Prompts"), this);
    auto *coreLayout = new QFormLayout(coreGroup);

    setupEnginePromptRow(coreLayout, QStringLiteral("analyzer"), i18n("Game Analyzer"));
    setupEnginePromptRow(coreLayout, QStringLiteral("lorekeeper_discovery"), i18n("LoreKeeper Discovery"));
    setupEnginePromptRow(coreLayout, QStringLiteral("lorekeeper_gen"), i18n("LoreKeeper Generation"));
    setupEnginePromptRow(coreLayout, QStringLiteral("synopsis_file"), i18n("File Synopsis"));
    setupEnginePromptRow(coreLayout, QStringLiteral("synopsis_folder"), i18n("Folder Synopsis"));
    setupEnginePromptRow(coreLayout, QStringLiteral("chargen"), i18n("Character Generator"));
    setupEnginePromptRow(coreLayout, QStringLiteral("sim_arbiter"), i18n("Simulation Arbiter"));
    setupEnginePromptRow(coreLayout, QStringLiteral("sim_griot"), i18n("Simulation Griot"));
    setupEnginePromptRow(coreLayout, QStringLiteral("sim_actor"), i18n("Simulation Actor"));

    layout->addWidget(coreGroup);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, i18n("Add Template"), i18n("Template Name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.isEmpty()) {
            PromptEditorDialog dialog(name, QString(), this);
            if (dialog.exec() == QDialog::Accepted) {
                auto *item = new QListWidgetItem(name, m_promptsList);
                item->setData(Qt::UserRole, dialog.content());
            }
        }
    });

    connect(viewTemplateBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_promptsList->currentItem();
        if (item) {
            QMessageBox::information(this, item->text(), item->data(Qt::UserRole).toString());
        }
    });

    connect(editTemplateBtn, &QPushButton::clicked, this, [this]() {
        auto *item = m_promptsList->currentItem();
        if (item) {
            PromptEditorDialog dialog(item->text(), item->data(Qt::UserRole).toString(), this);
            if (dialog.exec() == QDialog::Accepted) {
                item->setData(Qt::UserRole, dialog.content());
            }
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        delete m_promptsList->currentItem();
    });

    layout->addStretch();
    return tab;
}

QWidget* SettingsDialog::createAnalyzerTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);

    m_analyzerRunModeCombo = new QComboBox(this);
    m_analyzerRunModeCombo->addItems({i18n("Continuous (On Save)"), i18n("On-Demand"), i18n("Paused")});
    layout->addRow(i18n("Run Mode:"), m_analyzerRunModeCombo);

    // Provider + model for the analyzer live in the AI Agents tab
    // alongside every other agent. They used to be duplicated here and
    // both fields wrote to the same QSettings keys, so edits in either
    // place mirrored the other — visually confusing. Removed to give a
    // single source of truth.

    auto *hint = new QLabel(i18n(
        "Provider and model for the Game Analyzer are configured on the "
        "AI Agents tab."), tab);
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont.setItalic(true);
    hint->setFont(hintFont);
    layout->addRow(QString(), hint);

    return tab;
}

QWidget* SettingsDialog::createEditorTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_typewriterScrollingCheck = new QCheckBox(i18n("Enable Typewriter Scrolling (center cursor)"), this);
    layout->addWidget(m_typewriterScrollingCheck);

    layout->addStretch();
    return tab;
}

void SettingsDialog::load()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    m_typewriterScrollingCheck->setChecked(settings.value(QStringLiteral("editor/typewriterScrolling"), false).toBool());
    if (m_chatDeepSearchCheck) {
        m_chatDeepSearchCheck->setChecked(
            settings.value(QStringLiteral("chat/deep_search"), false).toBool());
    }

    // Model fields: no hardcoded defaults — empty means "not configured yet"
    // Placeholder text in the widget gives the user a hint without seeding QSettings.

    // Legacy migration: if the old global llm/embedding_model is set but no
    // per-provider slot is yet, copy it into the active provider's slot so
    // the user doesn't silently lose their configured model on first open.
    const QString legacyEmbedding = settings.value(QStringLiteral("llm/embedding_model")).toString();
    if (!legacyEmbedding.isEmpty()) {
        const LLMProvider active = static_cast<LLMProvider>(
            settings.value(QStringLiteral("llm/provider"), 0).toInt());
        const QString activeKey = LLMService::providerSettingsKey(active)
                                  + QStringLiteral("/embedding_model");
        if (settings.value(activeKey).toString().isEmpty()) {
            settings.setValue(activeKey, legacyEmbedding);
        }
        settings.remove(QStringLiteral("llm/embedding_model"));
    }

    // Load per-provider embedding models into each combo.
    for (auto it = m_providerEmbeddingCombos.constBegin();
         it != m_providerEmbeddingCombos.constEnd(); ++it) {
        const QString stored = settings.value(
            LLMService::providerSettingsKey(it.key())
            + QStringLiteral("/embedding_model")).toString();
        it.value()->setEditText(stored);
    }

    // Sync each provider's enable-toggle with the stored llm/{p}/enabled.
    for (auto it = m_providerToggles.constBegin();
         it != m_providerToggles.constEnd(); ++it) {
        it.value()->setChecked(LLMService::isProviderEnabled(it.key()));
    }

    m_openaiModelCombo->setEditText(settings.value(QStringLiteral("llm/openai/model")).toString());
    m_openaiEndpointEdit->setText(settings.value(QStringLiteral("llm/openai/endpoint"), QStringLiteral("https://api.openai.com/v1/chat/completions")).toString());
    m_openaiKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::OpenAI));

    m_anthropicModelCombo->setEditText(settings.value(QStringLiteral("llm/anthropic/model")).toString());
    m_anthropicEndpointEdit->setText(settings.value(QStringLiteral("llm/anthropic/endpoint"), QStringLiteral("https://api.anthropic.com/v1/messages")).toString());
    m_anthropicKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Anthropic));

    m_ollamaModelCombo->setEditText(settings.value(QStringLiteral("llm/ollama/model")).toString());
    m_ollamaEndpointEdit->setText(settings.value(QStringLiteral("llm/ollama/endpoint"), QStringLiteral("http://localhost:11434/api/chat")).toString());

    m_grokModelCombo->setEditText(settings.value(QStringLiteral("llm/grok/model"), QString()).toString());
    m_grokEndpointEdit->setText(settings.value(QStringLiteral("llm/grok/endpoint"), QStringLiteral("https://api.x.ai/v1/chat/completions")).toString());
    m_grokKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Grok));

    m_geminiModelCombo->setEditText(settings.value(QStringLiteral("llm/gemini/model"), QString()).toString());
    m_geminiEndpointEdit->setText(settings.value(QStringLiteral("llm/gemini/endpoint"), QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions")).toString());
    m_geminiKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::Gemini));

    m_lmstudioModelCombo->setEditText(settings.value(QStringLiteral("llm/lmstudio/model"), QString()).toString());
    m_lmstudioEndpointEdit->setText(settings.value(QStringLiteral("llm/lmstudio/endpoint"), QStringLiteral("http://localhost:1234/v1/chat/completions")).toString());
    m_lmstudioKeyEdit->setText(LLMService::instance().apiKey(LLMProvider::LMStudio));

    // For any provider that already has credentials configured, trigger a
    // silent model-list fetch so the combos populate on dialog open. We
    // don't block on this — the combos already carry the stored default,
    // so the user can work without waiting. The status labels update when
    // each fetch completes.
    for (LLMProvider p : {LLMProvider::OpenAI, LLMProvider::Anthropic,
                           LLMProvider::Ollama, LLMProvider::Grok,
                           LLMProvider::Gemini, LLMProvider::LMStudio}) {
        if (LLMService::instance().isProviderConfigured(p)) {
            testProviderConnection(p);
        }
    }

    m_analyzerRunModeCombo->setCurrentIndex(settings.value(QStringLiteral("analyzer/run_mode"), 2).toInt());

    // Load Agent Configurations
    for (auto it = m_agentConfigs.begin(); it != m_agentConfigs.end(); ++it) {
        const QString &id = it.key();
        auto &config = it.value();
        
        int providerIdx = settings.value(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_provider"), 
                                        settings.value(QStringLiteral("llm/provider"), 0)).toInt();
        config.providerCombo->setCurrentIndex(providerIdx);
        
        // Populate model combo for this provider
        updateModelCombos(static_cast<LLMProvider>(providerIdx));
        
        QString defaultModel;
        switch (static_cast<LLMProvider>(providerIdx)) {
            case LLMProvider::OpenAI: defaultModel = settings.value(QStringLiteral("llm/openai/model")).toString(); break;
            case LLMProvider::Anthropic: defaultModel = settings.value(QStringLiteral("llm/anthropic/model")).toString(); break;
            case LLMProvider::Ollama: defaultModel = settings.value(QStringLiteral("llm/ollama/model")).toString(); break;
            case LLMProvider::Grok: defaultModel = settings.value(QStringLiteral("llm/grok/model")).toString(); break;
            case LLMProvider::Gemini: defaultModel = settings.value(QStringLiteral("llm/gemini/model")).toString(); break;
            case LLMProvider::LMStudio: defaultModel = settings.value(QStringLiteral("llm/lmstudio/model")).toString(); break;
        }

        config.modelCombo->setEditText(settings.value(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_model"), defaultModel).toString());
    }

    // Load Core System Prompts
    m_enginePrompts[QStringLiteral("analyzer")].content = settings.value(QStringLiteral("analyzer/system_prompt"),
        QStringLiteral("You are an expert RPG game design analyzer.\n"
                       "Analyze the provided document for rule conflicts, ambiguities, and completeness gaps.\n"
                       "You must output ONLY a valid JSON array of objects. Do not include markdown code blocks or conversational text.\n"
                       "Format: [{\"line\": 0, \"severity\": \"error|warning|info\", \"message\": \"...\", \"references\": [{\"filePath\": \"...\", \"line\": 0}]}]")).toString();

    m_enginePrompts[QStringLiteral("lorekeeper_discovery")].content = settings.value(QStringLiteral("lorekeeper/discovery_prompt"),
        QStringLiteral("You are a world-building assistant. Extract a list of entities of type '%1' from the provided text.")).toString();

    m_enginePrompts[QStringLiteral("lorekeeper_gen")].content = settings.value(QStringLiteral("lorekeeper/gen_prompt"),
        QStringLiteral("You are an expert world-builder. %1\n\nReturn ONLY the updated Markdown content.")).toString();

    m_enginePrompts[QStringLiteral("synopsis_file")].content = settings.value(QStringLiteral("synopsis/file_prompt"),
        QStringLiteral("You are a senior RPG editor. Write a one-sentence hook/synopsis for this scene or document. Be atmospheric and concise.")).toString();

    m_enginePrompts[QStringLiteral("synopsis_folder")].content = settings.value(QStringLiteral("synopsis/folder_prompt"),
        QStringLiteral("You are an RPG project manager. Write a one-sentence summary for this folder (e.g. 'A collection of character backgrounds' or 'The core mechanics of combat').")).toString();

    m_enginePrompts[QStringLiteral("chargen")].content = settings.value(QStringLiteral("chargen/system_prompt"),
        QStringLiteral("You are an expert RPG character generator. Your goal is to create a character sheet that strictly follows the PROJECT RULES provided below.\n\n"
                       "PROJECT RULES:\n%1\n\n"
                       "TASK:\n"
                       "1. Output a valid JSON object representing the character sheet.\n"
                       "2. The JSON must include 'name', 'concept', 'stats', 'skills', 'equipment', and 'biography'.")).toString();

    m_enginePrompts[QStringLiteral("sim_arbiter")].content = settings.value(QStringLiteral("simulation/arbiter_prompt"),
        QStringLiteral("You are the Arbiter of a tabletop RPG simulation. Your job is to enforce the rules and update the world state.\n\n"
                       "SCENARIO CONTEXT:\n%1\n\n"
                       "RELEVANT RULES:\n%2\n\n"
                       "CURRENT SITUATION:\n"
                       "- Actor: %3\n"
                       "- Intent: %4\n"
                       "- World State: %5\n\n"
                       "TASK:\n"
                       "1. Evaluate the intent based on rules and context.\n"
                       "2. Generate a 'logical_patch' (JSON) to update the world state.\n"
                       "3. Write a 'mechanical_log' explaining the result.\n"
                       "4. Return ONLY a JSON object with 'logical_patch' and 'mechanical_log'.")).toString();

    m_enginePrompts[QStringLiteral("sim_griot")].content = settings.value(QStringLiteral("simulation/griot_prompt"),
        QStringLiteral("You are the Griot, an immersive storyteller for an RPG simulation.\n"
                       "Your task is to take dry mechanical results and turn them into cinematic prose.\n\n"
                       "INPUTS:\n"
                       "- Actor: %1\n"
                       "- Intent: %2\n"
                       "- Mechanical Result: %3\n"
                       "- World Changes: %4\n"
                       "- Current State: %5\n\n"
                       "STYLE:\n"
                       "- Evocative but concise.\n"
                       "- Second-person perspective if appropriate, or third-person cinematic.\n"
                       "- No mechanical jargon.")).toString();

    m_enginePrompts[QStringLiteral("sim_actor")].content = settings.value(QStringLiteral("simulation/actor_prompt"),
        QStringLiteral("You are an autonomous agent in a tabletop RPG simulation.\n"
                       "Name: %1\n"
                       "Motive: %2\n"
                       "Your Character Sheet (JSON): %3\n\n"
                       "TACTICAL AGGRESSION (Level %4): %5\n\n"
                       "CURRENT WORLD STATE (JSON): %6\n\n"
                       "RULES CONTEXT:\n%7\n\n"
                       "TASK:\n"
                       "Decide your next action based on your motive and the current state.\n"
                       "Return ONLY a JSON object: {\"intent\": \"...\", \"reasoning\": \"...\"}")).toString();

    // Load Prompts
    QString promptsJson = settings.value(QStringLiteral("llm/prompts")).toString();
    if (!promptsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(promptsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                auto *item = new QListWidgetItem(it.key(), m_promptsList);
                item->setData(Qt::UserRole, it.value().toString());
            }
        }
    } else {
        // Defaults
        auto *expand = new QListWidgetItem(i18n("Expand"), m_promptsList);
        expand->setData(Qt::UserRole, i18n("Please expand on the following worldbuilding text, adding more detail and lore."));
        auto *rewrite = new QListWidgetItem(i18n("Rewrite"), m_promptsList);
        rewrite->setData(Qt::UserRole, i18n("Please rewrite the following text for better flow and impact."));
        auto *summarize = new QListWidgetItem(i18n("Summarize"), m_promptsList);
        summarize->setData(Qt::UserRole, i18n("Please summarize the following rules or worldbuilding text."));
    }
}

void SettingsDialog::save()
{
    QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
    settings.setValue(QStringLiteral("editor/typewriterScrolling"), m_typewriterScrollingCheck->isChecked());
    if (m_chatDeepSearchCheck) {
        settings.setValue(QStringLiteral("chat/deep_search"),
                          m_chatDeepSearchCheck->isChecked());
    }

    // Derive the "active provider" (legacy llm/provider key, still read as
    // a fallback by many agents) from the top row of the draggable
    // provider list. That row is, by user-evident construction, the
    // preferred primary.
    if (m_providerRowsLayout && m_providerRowsLayout->count() > 0) {
        if (QLayoutItem *first = m_providerRowsLayout->itemAt(0)) {
            if (QWidget *row = first->widget()) {
                const QVariant tag = row->property("llmProvider");
                if (tag.isValid()) {
                    settings.setValue(QStringLiteral("llm/provider"), tag.toInt());
                }
            }
        }
    }

    // Per-provider embedding model (replaces the single global field).
    for (auto it = m_providerEmbeddingCombos.constBegin();
         it != m_providerEmbeddingCombos.constEnd(); ++it) {
        settings.setValue(
            LLMService::providerSettingsKey(it.key())
            + QStringLiteral("/embedding_model"),
            it.value()->currentText().trimmed());
    }
    // Retire the legacy global key — any value gets migrated into the
    // active provider's slot in load(); on subsequent saves it should not
    // come back.
    settings.remove(QStringLiteral("llm/embedding_model"));

    // Persist provider fallback order + per-provider enable flags from
    // the draggable list at the top of the LLM tab.
    saveProviderOrderList();

    settings.setValue(QStringLiteral("llm/openai/model"), m_openaiModelCombo->currentText());
    settings.setValue(QStringLiteral("llm/openai/endpoint"), m_openaiEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::OpenAI, m_openaiKeyEdit->text());

    settings.setValue(QStringLiteral("llm/anthropic/model"), m_anthropicModelCombo->currentText());
    settings.setValue(QStringLiteral("llm/anthropic/endpoint"), m_anthropicEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Anthropic, m_anthropicKeyEdit->text());

    settings.setValue(QStringLiteral("llm/ollama/model"), m_ollamaModelCombo->currentText());
    settings.setValue(QStringLiteral("llm/ollama/endpoint"), m_ollamaEndpointEdit->text());

    settings.setValue(QStringLiteral("llm/grok/model"), m_grokModelCombo->currentText());
    settings.setValue(QStringLiteral("llm/grok/endpoint"), m_grokEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Grok, m_grokKeyEdit->text());

    settings.setValue(QStringLiteral("llm/gemini/model"), m_geminiModelCombo->currentText());
    settings.setValue(QStringLiteral("llm/gemini/endpoint"), m_geminiEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::Gemini, m_geminiKeyEdit->text());

    settings.setValue(QStringLiteral("llm/lmstudio/model"), m_lmstudioModelCombo->currentText());
    settings.setValue(QStringLiteral("llm/lmstudio/endpoint"), m_lmstudioEndpointEdit->text());
    LLMService::instance().setApiKey(LLMProvider::LMStudio, m_lmstudioKeyEdit->text());

    settings.setValue(QStringLiteral("analyzer/run_mode"), m_analyzerRunModeCombo->currentIndex());
    // Analyzer provider/model are written by the AI Agents tab loop
    // below (createAgentRow "analyzer") — no duplicate write needed.

    // Save Agent Configurations
    for (auto it = m_agentConfigs.begin(); it != m_agentConfigs.end(); ++it) {
        const QString &id = it.key();
        auto &config = it.value();
        settings.setValue(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_provider"), config.providerCombo->currentIndex());
        settings.setValue(config.keyPrefix + QStringLiteral("/") + id + QStringLiteral("_model"), config.modelCombo->currentText());
    }

    // Save Core System Prompts
    settings.setValue(QStringLiteral("analyzer/system_prompt"), m_enginePrompts[QStringLiteral("analyzer")].content);
    settings.setValue(QStringLiteral("lorekeeper/discovery_prompt"), m_enginePrompts[QStringLiteral("lorekeeper_discovery")].content);
    settings.setValue(QStringLiteral("lorekeeper/gen_prompt"), m_enginePrompts[QStringLiteral("lorekeeper_gen")].content);
    settings.setValue(QStringLiteral("synopsis/file_prompt"), m_enginePrompts[QStringLiteral("synopsis_file")].content);
    settings.setValue(QStringLiteral("synopsis/folder_prompt"), m_enginePrompts[QStringLiteral("synopsis_folder")].content);
    settings.setValue(QStringLiteral("chargen/system_prompt"), m_enginePrompts[QStringLiteral("chargen")].content);
    settings.setValue(QStringLiteral("simulation/arbiter_prompt"), m_enginePrompts[QStringLiteral("sim_arbiter")].content);
    settings.setValue(QStringLiteral("simulation/griot_prompt"), m_enginePrompts[QStringLiteral("sim_griot")].content);
    settings.setValue(QStringLiteral("simulation/actor_prompt"), m_enginePrompts[QStringLiteral("sim_actor")].content);

    // Save Prompts
    QJsonObject obj;
    for (int i = 0; i < m_promptsList->count(); ++i) {
        auto *item = m_promptsList->item(i);
        obj[item->text()] = item->data(Qt::UserRole).toString();
    }
    settings.setValue(QStringLiteral("llm/prompts"), QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    // Force the QSettings backend to flush before any listener tries
    // to read; otherwise a same-process consumer may see stale values.
    settings.sync();

    // Notify all UI components that cache model/provider state so they
    // refresh from QSettings rather than continuing to send requests
    // with their old combo-box selection.
    LLMService::instance().notifySettingsChanged();
}

void SettingsDialog::setupEnginePromptRow(QFormLayout *layout, const QString &id, const QString &label)
{
    auto *rowLayout = new QHBoxLayout();
    
    auto *viewBtn = new QPushButton(i18n("View"), this);
    auto *editBtn = new QPushButton(i18n("Edit"), this);
    
    auto *status = new QLabel(this);
    status->setText(i18n("(Configured)"));
    status->setStyleSheet(QStringLiteral("font-size: 9px; color: gray;"));
    
    rowLayout->addWidget(viewBtn);
    rowLayout->addWidget(editBtn);
    rowLayout->addWidget(status);
    rowLayout->addStretch();
    
    layout->addRow(label + QStringLiteral(":"), rowLayout);
    
    EnginePrompt ep;
    ep.content = QString();
    ep.statusLabel = status;
    m_enginePrompts[id] = ep;
    
    connect(viewBtn, &QPushButton::clicked, this, [this, id, label]() {
        QMessageBox::information(this, label, m_enginePrompts[id].content);
    });
    
    connect(editBtn, &QPushButton::clicked, this, [this, id, label]() {
        openPromptEditor(id);
    });
}

void SettingsDialog::openPromptEditor(const QString &id)
{
    PromptEditorDialog dialog(id, m_enginePrompts[id].content, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_enginePrompts[id].content = dialog.content();
    }
}
