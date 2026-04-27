import React from 'react';
import { EditorContent, useEditor } from '@tiptap/react';
import StarterKit from '@tiptap/starter-kit';
import { Extension } from '@tiptap/core';
import { Plugin } from '@tiptap/pm/state';
import { Decoration, DecorationSet } from '@tiptap/pm/view';

const TOKEN_PATTERN = /\[@[A-Za-z0-9_]+\]?/g;

function escapeHtml(value) {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

function bodyToHtml(body = '') {
  const paragraphs = body.split(/\n{2,}/);
  return paragraphs
    .map(paragraph => `<p>${escapeHtml(paragraph).replace(/\n/g, '<br>')}</p>`)
    .join('');
}

function tokenKind(token, variables) {
  return variables.find(variable => variable.token === token)?.kind || 'Variable';
}

function tokenDecorations(doc, variables) {
  const decorations = [];

  doc.descendants((node, position) => {
    if (!node.isText || !node.text) return;

    for (const match of node.text.matchAll(TOKEN_PATTERN)) {
      const from = position + match.index;
      const to = from + match[0].length;
      decorations.push(Decoration.inline(from, to, {
        class: 'tok tiptap-token',
        'data-kind': tokenKind(match[0], variables),
      }));
    }
  });

  return DecorationSet.create(doc, decorations);
}

function createVariableTokenExtension(variables) {
  return Extension.create({
    name: 'mythosVariableTokens',

    addProseMirrorPlugins() {
      return [
        new Plugin({
          props: {
            decorations: state => tokenDecorations(state.doc, variables),
          },
        }),
      ];
    },
  });
}

export function MythosEditor({ document, variables, onDocumentChange }) {
  const lastDocumentId = React.useRef(document?.id);

  const editor = useEditor({
    extensions: [
      StarterKit.configure({
        heading: false,
        blockquote: false,
        codeBlock: false,
        horizontalRule: false,
      }),
      createVariableTokenExtension(variables),
    ],
    content: bodyToHtml(document?.body || ''),
    editorProps: {
      attributes: {
        class: 'manuscript-body tiptap-manuscript',
        spellcheck: 'true',
      },
    },
    onUpdate: ({ editor }) => {
      onDocumentChange?.({
        body: editor.getText({ blockSeparator: '\n\n' }),
        wordCount: editor.storage.characterCount?.words?.() || wordCount(editor.getText()),
      });
    },
  }, [document?.id, onDocumentChange, variables]);

  React.useEffect(() => {
    if (!editor || !document) return;
    if (lastDocumentId.current !== document.id) {
      lastDocumentId.current = document.id;
      editor.commands.setContent(bodyToHtml(document.body || ''), false);
    }
  }, [document, editor]);

  React.useEffect(() => {
    if (!editor || !document) return;
    const currentBody = editor.getText({ blockSeparator: '\n\n' });
    if (currentBody !== (document.body || '')) {
      editor.commands.setContent(bodyToHtml(document.body || ''), false);
    }
  }, [document?.body, editor]);

  return <EditorContent editor={editor}/>;
}

function wordCount(text = '') {
  return text.trim() ? text.trim().split(/\s+/).length : 0;
}
