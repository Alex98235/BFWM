import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  tutorialSidebar: [
    'index',
    'installation',
    'getting-started',
    {
      type: 'category',
      label: 'Configuration',
      collapsible: true,
      collapsed: false,
      items: [
        'configuration/defaults',
        'configuration/bfwm',
        'configuration/keybinds',
        'configuration/window-rules',
        'configuration/workspaces',
        'configuration/bar',
        'configuration/snackbar',
      ],
    },
    {
      type: 'category',
      label: 'Usage',
      collapsible: true,
      collapsed: false,
      items: [
        'usage/layouts',
        'usage/window-management',
        'usage/workspaces',
      ],
    },
    {
      type: 'category',
      label: 'Advanced',
      collapsible: true,
      collapsed: true,
      items: [
        'advanced/custom-lua-actions',
        'advanced/command-line',
      ],
    },
    'troubleshooting',
    'changelog',
  ],
};

export default sidebars;
