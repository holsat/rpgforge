import React from 'react';
import { EditorContent, useEditor } from '@tiptap/react';
import StarterKit from '@tiptap/starter-kit';
import Underline from '@tiptap/extension-underline';
import { Extension } from '@tiptap/core';
import { Plugin } from '@tiptap/pm/state';
import { Decoration, DecorationSet } from '@tiptap/pm/view';
import { Icon } from './icons.jsx';

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

function initialDocumentHtml(document) {
  return document?.contentHtml || bodyToHtml(document?.body || '');
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
        heading: { levels: [1, 2, 3] },
        codeBlock: false,
      }),
      Underline,
      createVariableTokenExtension(variables),
    ],
    content: initialDocumentHtml(document),
    editorProps: {
      attributes: {
        class: 'manuscript-body tiptap-manuscript',
        spellcheck: 'true',
      },
    },
    onUpdate: ({ editor }) => {
      onDocumentChange?.({
        body: editor.getText({ blockSeparator: '\n\n' }),
        contentHtml: editor.getHTML(),
        wordCount: editor.storage.characterCount?.words?.() || wordCount(editor.getText()),
      });
    },
  }, [document?.id, onDocumentChange, variables]);

  React.useEffect(() => {
    if (!editor || !document) return;
    if (lastDocumentId.current !== document.id) {
      lastDocumentId.current = document.id;
      editor.commands.setContent(initialDocumentHtml(document), false);
    }
  }, [document, editor]);

  React.useEffect(() => {
    if (!editor || !document) return;
    const currentBody = editor.getText({ blockSeparator: '\n\n' });
    if (currentBody !== (document.body || '')) {
      editor.commands.setContent(initialDocumentHtml(document), false);
    }
  }, [document?.body, document?.contentHtml, editor]);

  return (
    <>
      <RichTextToolbar editor={editor}/>
      <BubbleToolbar editor={editor}/>
      <EditorContent editor={editor}/>
    </>
  );
}

function wordCount(text = '') {
  return text.trim() ? text.trim().split(/\s+/).length : 0;
}

function ToolbarButton({ editor, icon, label, active, disabled, onClick }) {
  return (
    <button
      type="button"
      className={'rt-button' + (active ? ' active' : '')}
      disabled={disabled || !editor}
      title={label}
      aria-label={label}
      onMouseDown={event => {
        event.preventDefault();
        onClick?.();
      }}
    >
      <Icon name={icon} size={15}/>
    </button>
  );
}

function RichTextToolbar({ editor }) {
  const [stateTick, setStateTick] = React.useState(0);

  React.useEffect(() => {
    if (!editor) return undefined;
    const update = () => setStateTick(tick => tick + 1);
    editor.on('selectionUpdate', update);
    editor.on('transaction', update);
    return () => {
      editor.off('selectionUpdate', update);
      editor.off('transaction', update);
    };
  }, [editor]);

  void stateTick;

  return (
    <div className="richtext-toolbar" role="toolbar" aria-label="Manuscript formatting">
      <div className="rt-group">
        <ToolbarButton editor={editor} icon="undo" label="Undo" disabled={!editor?.can().undo()} onClick={() => editor.chain().focus().undo().run()}/>
        <ToolbarButton editor={editor} icon="redo" label="Redo" disabled={!editor?.can().redo()} onClick={() => editor.chain().focus().redo().run()}/>
      </div>
      <div className="rt-group">
        <ToolbarButton editor={editor} icon="bold" label="Bold" active={editor?.isActive('bold')} onClick={() => editor.chain().focus().toggleBold().run()}/>
        <ToolbarButton editor={editor} icon="italic" label="Italic" active={editor?.isActive('italic')} onClick={() => editor.chain().focus().toggleItalic().run()}/>
        <ToolbarButton editor={editor} icon="underline" label="Underline" active={editor?.isActive('underline')} onClick={() => editor.chain().focus().toggleUnderline().run()}/>
        <ToolbarButton editor={editor} icon="strike" label="Strikethrough" active={editor?.isActive('strike')} onClick={() => editor.chain().focus().toggleStrike().run()}/>
      </div>
      <div className="rt-group">
        <ToolbarButton editor={editor} icon="heading" label="Heading" active={editor?.isActive('heading', { level: 2 })} onClick={() => editor.chain().focus().toggleHeading({ level: 2 }).run()}/>
        <ToolbarButton editor={editor} icon="list" label="Bulleted list" active={editor?.isActive('bulletList')} onClick={() => editor.chain().focus().toggleBulletList().run()}/>
        <ToolbarButton editor={editor} icon="list-ordered" label="Numbered list" active={editor?.isActive('orderedList')} onClick={() => editor.chain().focus().toggleOrderedList().run()}/>
        <ToolbarButton editor={editor} icon="quote" label="Block quote" active={editor?.isActive('blockquote')} onClick={() => editor.chain().focus().toggleBlockquote().run()}/>
      </div>
    </div>
  );
}

function BubbleToolbar({ editor }) {
  const [visible, setVisible] = React.useState(false);
  const [stateTick, setStateTick] = React.useState(0);

  React.useEffect(() => {
    if (!editor) return undefined;
    const update = () => {
      const { from, to } = editor.state.selection;
      setVisible(editor.isFocused && from !== to);
      setStateTick(tick => tick + 1);
    };
    editor.on('selectionUpdate', update);
    editor.on('transaction', update);
    editor.on('focus', update);
    editor.on('blur', update);
    return () => {
      editor.off('selectionUpdate', update);
      editor.off('transaction', update);
      editor.off('focus', update);
      editor.off('blur', update);
    };
  }, [editor]);

  void stateTick;

  if (!editor || !visible) return null;

  return (
    <div className="richtext-bubble">
      <ToolbarButton editor={editor} icon="bold" label="Bold" active={editor.isActive('bold')} onClick={() => editor.chain().focus().toggleBold().run()}/>
      <ToolbarButton editor={editor} icon="italic" label="Italic" active={editor.isActive('italic')} onClick={() => editor.chain().focus().toggleItalic().run()}/>
      <ToolbarButton editor={editor} icon="underline" label="Underline" active={editor.isActive('underline')} onClick={() => editor.chain().focus().toggleUnderline().run()}/>
      <ToolbarButton editor={editor} icon="quote" label="Quote" active={editor.isActive('blockquote')} onClick={() => editor.chain().focus().toggleBlockquote().run()}/>
    </div>
  );
}
