/*
    RPG Forge
    Copyright (C) 2026  Sheldon L.

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

#include "charactergenerator.h"
#include "knowledgebase.h"
#include "llmservice.h"
#include "projectmanager.h"

#include <KLocalizedString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QFormLayout>
#include <QJsonDocument>
#include <QFileDialog>
#include <QDir>
#include <QSettings>

CharacterGenerator::CharacterGenerator(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("AI Character Generator"));
    setupUi();
}

CharacterGenerator::~CharacterGenerator() = default;

void CharacterGenerator::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    m_stack = new QStackedWidget(this);

    m_stack->addWidget(createConceptStep());
    m_stack->addWidget(createReviewStep());

    mainLayout->addWidget(m_stack);

    auto *btnLayout = new QHBoxLayout();
    m_prevBtn = new QPushButton(i18n("Back"), this);
    m_prevBtn->setEnabled(false);
    connect(m_prevBtn, &QPushButton::clicked, this, &CharacterGenerator::prevStep);
    
    m_nextBtn = new QPushButton(i18n("Generate"), this);
    connect(m_nextBtn, &QPushButton::clicked, this, &CharacterGenerator::nextStep);
    
    m_saveBtn = new QPushButton(i18n("Save Character"), this);
    m_saveBtn->setVisible(false);
    connect(m_saveBtn, &QPushButton::clicked, this, &CharacterGenerator::saveCharacter);

    btnLayout->addWidget(m_prevBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_nextBtn);

    mainLayout->addLayout(btnLayout);

    setMinimumSize(550, 450);
}

QWidget* CharacterGenerator::createConceptStep()
{
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    layout->addWidget(new QLabel(i18n("Step 1: Character Concept"), this));
    
    auto *form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(i18n("e.g. Valerius the Bold"));
    form->addRow(i18n("Character Name:"), m_nameEdit);
    layout->addLayout(form);

    layout->addWidget(new QLabel(i18n("Describe your character concept:"), this));
    m_conceptEdit = new QTextEdit(this);
    m_conceptEdit->setPlaceholderText(i18n("Describe their background, role, and any specific traits you want. The AI will apply your project rules automatically."));
    layout->addWidget(m_conceptEdit);

    return w;
}

QWidget* CharacterGenerator::createReviewStep()
{
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    layout->addWidget(new QLabel(i18n("Step 2: Review and Refine"), this));
    
    m_resultEdit = new QTextEdit(this);
    layout->addWidget(m_resultEdit);

    auto *refineForm = new QFormLayout();
    m_refineEdit = new QLineEdit(this);
    m_refineEdit->setPlaceholderText(i18n("e.g. Give him more strength, or add a magic item..."));
    refineForm->addRow(i18n("Refinement:"), m_refineEdit);
    layout->addLayout(refineForm);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();
    layout->addWidget(m_progressBar);

    return w;
}

void CharacterGenerator::nextStep()
{
    if (m_stack->currentIndex() == 0) {
        generateCharacter();
    }
}

void CharacterGenerator::prevStep()
{
    m_stack->setCurrentIndex(0);
    m_prevBtn->setEnabled(false);
    m_nextBtn->setText(i18n("Generate"));
    m_saveBtn->setVisible(false);
}

void CharacterGenerator::generateCharacter()
{
    m_progressBar->show();
    m_nextBtn->setEnabled(false);
    
    QString charConcept = m_conceptEdit->toPlainText();
    QString refinement = m_refineEdit->text();
    QString currentJson = m_resultEdit->toPlainText();
    m_refineEdit->clear();

    QString query = QStringLiteral("character creation rules attributes skills equipment");

    // RAG: Look up rules first
    KnowledgeBase::instance().search(query, 5, QString(), [this, charConcept, refinement, currentJson](const QList<SearchResult> &results) {
        QString rulesContext;
        for (const auto &res : results) {
            rulesContext += QStringLiteral("--- Rule: %1 ---\n%2\n\n").arg(res.heading, res.content);
        }

        QString systemPrompt = QStringLiteral(
            "You are an expert RPG character generator. Your goal is to create a character sheet that strictly follows the PROJECT RULES provided below.\n\n"
            "PROJECT RULES:\n%1\n\n"
            "TASK:\n"
            "1. Output a valid JSON object representing the character sheet.\n"
            "2. The JSON must include 'name', 'concept', 'stats', 'skills', 'equipment', and 'biography'.\n"
            "Output ONLY the JSON object. Do not include markdown code blocks or conversational text."
        ).arg(rulesContext);

        QString userPrompt;
        if (currentJson.isEmpty()) {
            userPrompt = QStringLiteral("Character Name: %1\nConcept: %2").arg(m_nameEdit->text(), charConcept);
        } else {
            userPrompt = QStringLiteral("CURRENT DATA:\n%1\n\nINSTRUCTION: %2").arg(currentJson, refinement.isEmpty() ? QStringLiteral("Regenerate or improve.") : refinement);
        }

        LLMRequest req;
        QSettings settings(QStringLiteral("RPGForge"), QStringLiteral("RPGForge"));
        req.provider = static_cast<LLMProvider>(settings.value(QStringLiteral("llm/provider"), 0).toInt());
        req.model = (req.provider == LLMProvider::OpenAI) ? settings.value(QStringLiteral("llm/openai/model")).toString() : settings.value(QStringLiteral("llm/ollama/model")).toString();
        
        req.messages.append({QStringLiteral("system"), systemPrompt});
        req.messages.append({QStringLiteral("user"), userPrompt});
        req.stream = false;

        LLMService::instance().sendNonStreamingRequest(req, [this](const QString &response) {
            m_progressBar->hide();
            m_nextBtn->setEnabled(true);
            
            if (response.isEmpty()) return;

            // Clean JSON
            QString cleanJson = response.trimmed();
            if (cleanJson.startsWith(QLatin1String("```json"))) {
                cleanJson = cleanJson.mid(7);
                if (cleanJson.endsWith(QLatin1String("```"))) cleanJson.chop(3);
            }

            m_resultEdit->setPlainText(cleanJson.trimmed());
            m_stack->setCurrentIndex(1);
            m_prevBtn->setEnabled(true);
            m_nextBtn->setText(i18n("Regenerate"));
            m_saveBtn->setVisible(true);
        });
    });
}

void CharacterGenerator::saveCharacter()
{
    QString defaultName = m_nameEdit->text().isEmpty() ? QStringLiteral("new_character") : m_nameEdit->text().toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    QString path = QFileDialog::getSaveFileName(this, i18n("Save Character Sheet"), 
        QDir(ProjectManager::instance().projectPath()).absoluteFilePath(defaultName + QStringLiteral(".json")),
        i18n("JSON Files (*.json);;Markdown Files (*.md)"));

    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_resultEdit->toPlainText().toUtf8());
            accept();
        }
    }
}
